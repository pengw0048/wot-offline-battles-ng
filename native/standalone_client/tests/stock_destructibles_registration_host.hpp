// Only registry calls are bounded here. The registered endpoint is the complete
// production StaticState, including its actual geometry and lifecycle methods.
BW_BEGIN_NAMESPACE
class StockDestructiblesOwner {};
std::weak_ptr<StockDestructibleObjects> registeredDestructibles;
struct DestructionCallback {unsigned instance;bool shot,havok;};
std::vector<DestructionCallback> destructionCallbacks;
std::function<void(uint32)> onDestruction;
StockDestructibles acquireStockDestructibles(ClientSpace*){return std::make_shared<StockDestructiblesOwner>();}
void registerStockDestructibleObjects(const StockDestructibles&,const void*,bool tree,const std::shared_ptr<StockDestructibleObjects>& objects){assert(!tree);registeredDestructibles=objects;}
void unregisterStockDestructibleObjects(const StockDestructibles& owner,const void*,bool tree){assert(!tree);if(owner)registeredDestructibles.reset();}
void playStockModuleDestructionAnimation(const StockDestructibles&,const void*,uint32 index,bool shot,bool havok){destructionCallbacks.push_back({index,shot,havok});auto notify=std::move(onDestruction);onDestruction=nullptr;if(notify)notify(index);}
BW_END_NAMESPACE
