#pragma once
#include "cstdmf/bw_namespace.hpp"
#include "moo/device_callback.hpp"
#include "moo/effect_material.hpp"
#include "moo/render_target.hpp"
#include "pyscript/script_math.hpp"
#include <cstdint>
#include <memory>
#include <vector>

BW_BEGIN_NAMESPACE
extern int StockFlashBang_token;
class StockFlashBangSupport : public Moo::DeviceCallback
{
public:
    StockFlashBangSupport();
    ~StockFlashBangSupport();
    bool initialise();
    void add(Vector4ProviderPtr provider);
    void remove(Vector4ProviderPtr provider);
    void tick();
    bool draw();
    void close();
    void reopen();
    void createManagedObjects() override;
    void deleteManagedObjects() override;
    void deleteUnmanagedObjects() override;
private:
    std::vector<Vector4ProviderPtr> animations_;
    Vector4 colour_;
    Moo::EffectMaterialPtr material_;
    Moo::RenderTargetPtr history_;
    std::uint64_t generation_ = 0;
    bool open_ = true;
    bool historyValid_ = false;
};
void initStockFlashBangAnimations();
void finiStockFlashBangAnimations();
BW_END_NAMESPACE
