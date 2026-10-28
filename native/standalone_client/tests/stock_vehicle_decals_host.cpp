#include "stock_vehicle_decals_host.hpp"
// The actual SDK already declares this type through its render headers.
namespace BW { class Renderer {}; }
#include "stock_vehicle_decals.cpp"
namespace BW {
std::vector<StockDeferredDecalPacket> submitted;
unsigned stockDeferredDecalVisibleLimit(){return 1024;}
void clearStockDeferredDecalTextures(){}
bool drawStockDeferredDecalPackets(const std::vector<StockDeferredDecalPacket>& packets,unsigned){submitted.insert(submitted.end(),packets.begin(),packets.end());return true;}
unsigned word(unsigned offset){unsigned v;std::memcpy(&v,submitted.back().record.data()+offset,4);return v;}
float scalar(unsigned offset){float v;std::memcpy(&v,submitted.back().record.data()+offset,4);return v;}
}
int main(){using namespace BW;
 WGOcclusionDecal before;assert(!before.create("","","","x",0,4,30,~0u,2));
 assert(initialiseStockVehicleDecals());assert(initialiseStockVehicleDecals());
 WGOcclusionDecal ao;WGShadowForwardDecal shadow;Moo::DrawContext context;
 Moo::BaseTexture::available=false;assert(!ao.create("","","","x",1,4,30,~0u,2));
 Moo::BaseTexture::available=true;assert(ao.create("ignored","ignored","ignored","x",3,4,30,~0u,2));
 assert(!shadow.setup("x",3,5,62));assert(shadow.setup("x",6,5,62));
 Matrix world=Matrix::identity;world.translation(Vector3(2,3,4));Matrix local=Matrix::identity;local.translation(Vector3(1,2,3));
 ao.setLocalTransform(local);ao.draw(context,world);assert(renderer->queue.empty());
 beginStockVehicleDecals();ao.tick(0);assert(ao.needsUpdate());ao.draw(context,world);shadow.draw(context,world);
 assert(!ao.needsUpdate());ao.tick(0);assert(ao.needsUpdate());
 assert(drawStockVehicleDecalsPhase(1));assert(submitted.size()==1&&word(0)==2&&word(4)==3&&word(8)==30);
 assert(scalar(48)==3&&scalar(52)==5&&scalar(56)==7&&scalar(88)==1);
 submitted.clear();assert(drawStockVehicleDecalsPhase(0));assert(submitted.size()==1&&word(0)==6&&word(8)==62);
 submitted.clear();ao.detach();assert(drawStockVehicleDecalsPhase(1)&&submitted.empty());
 endStockVehicleDecals();assert(renderer->queue.empty()&&!renderer->frame);
 beginStockVehicleDecals();context.pass=2;ao.draw(context,world);assert(renderer->queue.empty());context.pass=1;
 Moo::rc().reflection=true;ao.draw(context,world);assert(renderer->queue.empty());Moo::rc().reflection=false;
 shadow.collideWithScene(true);hostSpace->hit=12;shadow.draw(context,world);
 assert(hostSpace->start.y==13&&hostSpace->end.y==-997);assert(renderer->queue.back().world.applyToOrigin().y==1);
 renderer->deleteUnmanagedObjects();assert(renderer->queue.empty()&&!renderer->frame);
 beginStockVehicleDecals();ao.draw(context,world);BoundingBox::culled=true;assert(drawStockVehicleDecalsPhase(1));assert(submitted.empty());BoundingBox::culled=false;
 endStockVehicleDecals();beginStockVehicleDecals();world.translation(Vector3(151,0,0));ao.draw(context,world);assert(drawStockVehicleDecalsPhase(1)&&submitted.empty());
 setStockVehicleDecalsQuality(4);assert(drawStockVehicleDecalsPhase(1)&&submitted.empty());setStockVehicleDecalsQuality(0);
 endStockVehicleDecals();auto old=hostSpace;hostSpace=std::make_shared<ClientSpace>();beginStockVehicleDecals();
 world=Matrix::identity;ao.draw(context,world);shadow.draw(context,world);assert(renderer->queue.empty());
 assert(!ao.create("","","","x",0,4,30,0,0));
 auto fresh=std::unique_ptr<WGOcclusionDecal>(new WGOcclusionDecal);assert(fresh->create("","","","x",0,4,30,0,0));
 fresh->draw(context,world);assert(renderer->queue.size()==1);fresh.reset();assert(drawStockVehicleDecalsPhase(1)&&submitted.empty());
 WGShadowForwardDecal retired;assert(retired.setup("x",6,0,62));retired.draw(context,world);
 finaliseStockVehicleDecals();assert(!renderer);finaliseStockVehicleDecals();
 assert(initialiseStockVehicleDecals());beginStockVehicleDecals();retired.draw(context,world);assert(renderer->queue.empty());
 endStockVehicleDecals();finaliseStockVehicleDecals();
}
