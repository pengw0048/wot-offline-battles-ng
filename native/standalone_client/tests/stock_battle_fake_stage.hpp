// Host AS3 display tree for the battle marker canvas and minimap cores.
#pragma once

#include "wot/stock_battle_minimap.hpp"
#include "wot/stock_battle_presentation.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fake
{
using wot::stock_battle_presentation::GfxMatrix3;
using wot::stock_battle_presentation::GfxMatrix4;
using wot::stock_battle_presentation::Matrix44;

struct Provider
{
    Matrix44 world;
};
typedef std::shared_ptr<Provider> ProviderPtr;

// A display object handle; 0 is an undefined GFx::Value.
struct Clip
{
    int node = 0;
};

struct Node
{
    std::string name;
    std::string symbol;
    int parent = 0;
    std::vector<int> children;
    std::map<std::string, int> members;
    bool hasDisplay3D = false, visible = true, hasMatrix3D = false;
    GfxMatrix4 projection = {};
    GfxMatrix3 matrix3D = {};
    double x = 0, y = 0, rotation = 0, xScale = 100, yScale = 100, alpha = 100;
    unsigned displayCalls = 0;
};

class Stage
{
public:
    typedef fake::Clip Clip;
    typedef ProviderPtr Provider;

    Stage() { nodes.resize(1); }

    int create(const std::string& name, int parent)
    {
        Node node;
        node.name = name;
        node.parent = parent;
        nodes.push_back(node);
        const int id = static_cast<int>(nodes.size()) - 1;
        if (parent)
        {
            nodes[parent].children.push_back(id);
            nodes[parent].members[name] = id;
        }
        return id;
    }

    // Symbols the fake library exports and their authored children.
    std::map<std::string, std::vector<std::string> > symbols;
    std::vector<Node> nodes;
    std::vector<std::string> log;

    bool isObject(const Clip& clip) const { return clip.node != 0; }

    bool getMember(const Clip& clip, const char* name, Clip& out)
    {
        if (!clip.node)
            return false;
        std::map<std::string, int>::const_iterator found = nodes[clip.node].members.find(name);
        if (found == nodes[clip.node].members.end())
            return false;
        out.node = found->second;
        return true;
    }

    // ObjectInterface::AttachMovie: depth < 0 or past the end appends.
    bool attachMovie(Clip& container, const char* symbol, const char* name, int depth, Clip& out)
    {
        char line[256];
        std::snprintf(line, sizeof(line), "attach %s %s %d", symbol, name, depth);
        log.push_back(line);
        if (!container.node || !symbols.count(symbol))
            return false;
        Node node;
        node.name = name;
        node.symbol = symbol;
        node.parent = container.node;
        nodes.push_back(node);
        const int id = static_cast<int>(nodes.size()) - 1;
        std::vector<int>& children = nodes[container.node].children;
        const std::size_t index = depth < 0 || static_cast<std::size_t>(depth) > children.size() ?
            children.size() : static_cast<std::size_t>(depth);
        children.insert(children.begin() + static_cast<std::ptrdiff_t>(index), id);
        for (std::size_t i = 0; i < symbols[symbol].size(); ++i)
            create(symbols[symbol][i], id);
        out.node = id;
        return true;
    }

    void removeFromParent(Clip& clip)
    {
        log.push_back("remove " + nodes[clip.node].name);
        detach(clip.node);
    }

    void addChild(Clip& container, Clip& clip)
    {
        log.push_back("addChild " + nodes[container.node].name + " " + nodes[clip.node].name);
        detach(clip.node);
        nodes[clip.node].parent = container.node;
        nodes[container.node].children.push_back(clip.node);
    }

    void swapChildren(Clip& first, Clip& second)
    {
        log.push_back("swap " + nodes[first.node].name + " " + nodes[second.node].name);
        std::vector<int>& children = nodes[nodes[first.node].parent].children;
        std::vector<int>::iterator a = std::find(children.begin(), children.end(), first.node);
        std::vector<int>::iterator b = std::find(children.begin(), children.end(), second.node);
        std::iter_swap(a, b);
    }

    bool matrix(const Provider& provider, Matrix44& out)
    {
        out = provider->world;
        return true;
    }

    void setMatrix3D(Clip& clip, const GfxMatrix3& matrix)
    {
        Node& node = nodes[clip.node];
        node.hasMatrix3D = true;
        node.matrix3D = matrix;
    }

    void setDisplay3D(Clip& clip, const GfxMatrix4& projection, bool visible)
    {
        Node& node = nodes[clip.node];
        node.hasDisplay3D = true;
        node.projection = projection;
        node.visible = visible;
        ++node.displayCalls;
    }

    void setVisible(Clip& clip, bool visible)
    {
        nodes[clip.node].visible = visible;
        ++nodes[clip.node].displayCalls;
    }

    void setScale(Clip& clip, double scale)
    {
        if (!clip.node)
            return;
        nodes[clip.node].xScale = nodes[clip.node].yScale = scale;
    }

    void setAlpha(Clip& clip, double alpha)
    {
        if (!clip.node)
            return;
        nodes[clip.node].alpha = alpha;
    }

    void setDisplay(Clip& clip, const wot::stock_battle_minimap::EntryDisplay& display)
    {
        Node& node = nodes[clip.node];
        if (display.hasPosition)
        {
            node.x = display.x;
            node.y = display.y;
        }
        if (display.hasRotation)
            node.rotation = display.rotation;
        if (display.hasScale)
        {
            node.xScale = display.xScale;
            node.yScale = display.yScale;
        }
        node.visible = display.visible;
        ++node.displayCalls;
    }

    std::vector<std::string> childNames(int parent) const
    {
        std::vector<std::string> names;
        for (std::size_t i = 0; i < nodes[parent].children.size(); ++i)
            names.push_back(nodes[nodes[parent].children[i]].name);
        return names;
    }

private:
    void detach(int id)
    {
        if (!nodes[id].parent)
            return;
        std::vector<int>& children = nodes[nodes[id].parent].children;
        children.erase(std::find(children.begin(), children.end(), id));
        nodes[id].parent = 0;
    }
};

inline ProviderPtr translated(float x, float y, float z)
{
    ProviderPtr provider(new Provider());
    Matrix44 m = {{{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, 0.f}, {0.f, 0.f, 1.f, 0.f},
        {x, y, z, 1.f}}};
    provider->world = m;
    return provider;
}
}
