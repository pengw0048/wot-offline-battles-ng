// Host scenarios for the WGVehicleMarkersCanvas core; prints one JSON document.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "stock_battle_fake_stage.hpp"
#include "wot/stock_battle_marker_canvas.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace wot::stock_battle_marker_canvas;
typedef Canvas<fake::Stage> FakeCanvas;

static void printFloats(const float* values, int count)
{
    std::printf("[");
    for (int i = 0; i < count; ++i)
        std::printf("%s%.9g", i ? "," : "", static_cast<double>(values[i]));
    std::printf("]");
}

static Camera camera()
{
    // View: identity rotation, camera at (0, 2, -10). D3DXMatrixPerspectiveFovLH
    // (60 degrees, 16:9, near 0.5, far 1500) times the view.
    Camera result = {};
    const Matrix44 view = {{{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, 0.f}, {0.f, 0.f, 1.f, 0.f},
        {0.f, -2.f, 10.f, 1.f}}};
    const float yScale = 1.7320508f, xScale = yScale / (16.f / 9.f);
    const float zf = 1500.f, zn = 0.5f, q = zf / (zf - zn);
    const Matrix44 projection = {{{xScale, 0.f, 0.f, 0.f}, {0.f, yScale, 0.f, 0.f},
        {0.f, 0.f, q, 1.f}, {0.f, 0.f, -zn * q, 0.f}}};
    Matrix44 combined = {};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            combined.m[r][c] = view.m[r][0] * projection.m[0][c] + view.m[r][1] * projection.m[1][c] +
                view.m[r][2] * projection.m[2][c] + view.m[r][3] * projection.m[3][c];
    result.view = view;
    result.viewProjection = combined;
    result.nearPlane = zn;
    result.farPlane = zf;
    result.zoomFactor = 1.f;
    result.halfScreenWidth = 960.f;
    result.halfScreenHeight = 540.f;
    return result;
}

int main()
{
    fake::Stage stage;
    stage.symbols["VehicleMarker"] = std::vector<std::string>{"marker", "bgShadow"};
    stage.symbols["BareMarker"] = std::vector<std::string>();
    FakeCanvas canvas(stage);
    fake::Clip noRoot;
    fake::Clip root;
    root.node = stage.create("vehicleMarkersCanvas", 0);

    // Defaults and the missing-root containment.
    assert(canvas.scaleProperties().minimum == 50.f && canvas.scaleProperties().maximum == 100.f);
    assert(canvas.scaleProperties().initial == 100.f && canvas.scaleProperties().speed == -0.3f);
    unsigned id = 99;
    assert(canvas.addMarker(noRoot, fake::translated(0, 0, 0), "VehicleMarker", true, id) ==
        FakeCanvas::NO_ROOT && id == 99 && stage.log.empty());

    // IDs start at 0 and depths follow; inactive placeholders take no provider.
    fake::ProviderPtr near = fake::translated(1.f, 1.f, 90.f);
    fake::ProviderPtr far = fake::translated(-30.f, 4.f, 600.f);
    fake::ProviderPtr middle = fake::translated(12.f, 0.f, 240.f);
    unsigned ids[3];
    assert(canvas.addMarker(root, far, "VehicleMarker", true, ids[0]) == FakeCanvas::ADDED);
    assert(canvas.addMarker(root, fake::ProviderPtr(), "VehicleMarker", false, ids[1]) ==
        FakeCanvas::ADDED);
    assert(canvas.addMarker(root, middle, "VehicleMarker", true, ids[2]) == FakeCanvas::ADDED);
    assert(ids[0] == 0 && ids[1] == 1 && ids[2] == 2);
    assert(stage.log[0] == "attach VehicleMarker VehicleMarker0 0");
    assert(stage.log[2] == "attach VehicleMarker VehicleMarker2 2");
    assert(canvas.find(0)->depth == 0 && canvas.find(2)->depth == 2);
    assert(canvas.find(0)->inner.node && canvas.find(0)->shadow.node);

    // An unknown symbol consumes neither the ID nor the depth.
    assert(canvas.addMarker(root, near, "MissingSymbol", true, id) == FakeCanvas::ATTACH_FAILED);
    assert(canvas.size() == 3);

    // Remove then reuse last-in first-out; a second removal is refused.
    assert(canvas.delMarker(1) && !canvas.delMarker(1) && canvas.find(1) == NULL);
    assert(stage.log.back() == "remove VehicleMarker1");
    assert(canvas.addMarker(root, fake::ProviderPtr(), "VehicleMarker", false, id) ==
        FakeCanvas::ADDED && id == 1 && canvas.find(1)->depth == 1);
    assert(stage.log.back() == "attach VehicleMarker VehicleMarker1 1");

    // Active markers keep a provider when asked to clear it; inactive ones drop it.
    canvas.setMatrix(0, fake::ProviderPtr());
    assert(canvas.find(0)->provider == far);
    assert(canvas.setActive(0, false) && !canvas.setActive(77, true));
    canvas.setMatrix(0, fake::ProviderPtr());
    assert(!canvas.find(0)->provider);
    canvas.setMatrix(0, far);
    canvas.setMatrix(42, far);
    assert(canvas.setActive(0, true) && canvas.find(0)->provider == far);
    canvas.setMatrix(1, near);
    assert(canvas.find(1)->provider == near && !canvas.find(1)->active);

    // Frame 1: marker 1 is inactive, so only its visibility is written.
    const Camera view = camera();
    canvas.tick(view);
    const fake::Node& hidden = stage.nodes[canvas.find(1)->clip.node];
    assert(!hidden.visible && hidden.displayCalls == 1 && !hidden.hasDisplay3D);
    assert(stage.childNames(root.node) ==
        (std::vector<std::string>{"VehicleMarker0", "VehicleMarker1", "VehicleMarker2"}));
    // Visible: 0 (z 610, depth 0) and 2 (z 250, depth 2): nearest already on top.
    assert(stage.log.back() == "attach VehicleMarker VehicleMarker1 1");

    // Activating marker 1 (z 100, depth 1) moves it above marker 2 with one swap.
    assert(canvas.setActive(1, true));
    canvas.tick(view);
    assert(stage.log.back() == "swap VehicleMarker1 VehicleMarker2");
    assert(canvas.find(1)->depth == 2 && canvas.find(2)->depth == 1 && canvas.find(0)->depth == 0);
    assert(stage.childNames(root.node) ==
        (std::vector<std::string>{"VehicleMarker0", "VehicleMarker2", "VehicleMarker1"}));
    const std::size_t logSize = stage.log.size();
    canvas.tick(view);
    assert(stage.log.size() == logSize);

    // A marker behind the camera is hidden and takes no part in sorting.
    fake::ProviderPtr behind = fake::translated(0.f, 0.f, -40.f);
    unsigned behindID = 0;
    assert(canvas.addMarker(root, behind, "VehicleMarker", true, behindID) == FakeCanvas::ADDED &&
        behindID == 3);
    canvas.tick(view);
    assert(!stage.nodes[canvas.find(3)->clip.node].visible && !canvas.find(3)->inFront);
    assert(stage.log.size() == logSize + 1);

    // An active marker without a provider keeps its last presentation.
    unsigned bareID = 0;
    assert(canvas.addMarker(root, fake::ProviderPtr(), "BareMarker", true, bareID) ==
        FakeCanvas::ADDED);
    canvas.tick(view);
    assert(stage.nodes[canvas.find(bareID)->clip.node].displayCalls == 0);
    canvas.setMatrix(bareID, fake::translated(-3.f, 0.f, 150.f));
    canvas.tick(view);
    assert(stage.nodes[canvas.find(bareID)->clip.node].hasDisplay3D);

    // Sniper zoom and custom properties for the numeric reference below.
    canvas.scaleProperties().minimum = 40.f;
    canvas.scaleProperties().speed = 0.45f;
    canvas.alphaProperties().maximum = 80.f;
    canvas.alphaProperties().speed = 2.f;
    Camera zoomed = view;
    zoomed.zoomFactor = 0.25f;
    canvas.tick(zoomed);

    std::printf("{\"camera\":{\"view\":");
    printFloats(&view.view.m[0][0], 16);
    std::printf(",\"viewProjection\":");
    printFloats(&view.viewProjection.m[0][0], 16);
    std::printf(",\"near\":%.9g,\"far\":%.9g,\"zoom\":%.9g,\"halfWidth\":%.9g,\"halfHeight\":%.9g},",
        view.nearPlane, view.farPlane, zoomed.zoomFactor, view.halfScreenWidth,
        view.halfScreenHeight);
    std::printf("\"scale\":");
    printFloats(&canvas.scaleProperties().minimum, 4);
    std::printf(",\"alpha\":");
    printFloats(&canvas.alphaProperties().minimum, 4);
    std::printf(",\"markers\":[");
    const unsigned order[] = {0, 1, 2, 3};
    for (int i = 0; i < 4; ++i)
    {
        FakeCanvas::Marker* marker = canvas.find(order[i]);
        const fake::Node& clip = stage.nodes[marker->clip.node];
        std::printf("%s{\"id\":%u,\"world\":", i ? "," : "", order[i]);
        Matrix44 world;
        stage.matrix(marker->provider, world);
        printFloats(&world.m[0][0], 16);
        std::printf(",\"viewZ\":%.9g,\"visible\":%s,\"depth\":%u,\"projection\":",
            marker->viewZ, clip.visible ? "true" : "false", marker->depth);
        printFloats(&clip.projection.m[0][0], 16);
        std::printf(",\"matrix3D\":");
        printFloats(&clip.matrix3D.m[0][0], 12);
        std::printf(",\"scale\":%.17g,\"alpha\":%.17g}",
            stage.nodes[marker->inner.node].xScale, stage.nodes[marker->shadow.node].alpha);
    }
    std::printf("],\"children\":[");
    const std::vector<std::string> names = stage.childNames(root.node);
    for (std::size_t i = 0; i < names.size(); ++i)
        std::printf("%s\"%s\"", i ? "," : "", names[i].c_str());
    std::printf("]}\n");

    // Teardown releases every marker without touching the stage.
    const std::size_t finalLog = stage.log.size();
    canvas.clear();
    assert(canvas.size() == 0 && stage.log.size() == finalLog);
    return 0;
}
