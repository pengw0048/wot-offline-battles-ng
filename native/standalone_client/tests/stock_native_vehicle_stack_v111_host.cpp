#include "stock_native_vehicle_stack.hpp"
namespace s=stock_native_vehicle_stack;
extern "C" {
void stack_ledger(const double*rows,int count,double*out){s::Ledger ledger;for(int i=0;i<count;++i){const auto*p=rows+i*8;
    ledger.advance({false,1},{true,2},p[0],p[1]!=0.,p[2]!=0.,p[3],p[4]);ledger.strike({false,1},{true,2},int(p[5]),int(p[6]));}
    const auto row=ledger.publication().front();out[0]=row.upper_total;out[1]=row.lower_total;out[2]=row.lower_environment_total;out[3]=double(row.upper_impact_total);out[4]=row.contact_seconds;}
void stack_descent(const double*rows,int count,double*out){s::Descent descent;for(int i=0;i<count;++i){const auto*p=rows+i*8;
    descent.observe(p[0]!=0.,p[1],p[2]!=0.,p[3]==99999.?std::nullopt:std::optional<double>{p[3]},p[4]);const s::Identity carrier{p[5]!=0.,int(p[6])};
    const bool admitted=descent.admit(carrier,p[7],true,p[4]);out[i*3]=admitted;out[i*3+1]=descent.active;out[i*3+2]=double(descent.sequence);if(admitted)descent.commit(carrier);}}
void stack_match(const double*points,int point_count,const double*hits,int hit_count,double*out){std::vector<s::Point>contacts;std::vector<s::Surface>surfaces;
    for(int i=0;i<point_count;++i)contacts.push_back({points[3*i],points[3*i+1],points[3*i+2]});
    for(int i=0;i<hit_count;++i){const auto*p=hits+i*8;surfaces.push_back({{p[0]!=0.,int(p[1])},{p[2],p[3],p[4]},{p[5],p[6],p[7]}});}
    const auto result=s::settled_carrier(contacts,surfaces);out[0]=result.has_value();if(result){out[1]=result->carrier.human;out[2]=double(result->carrier.id);out[3]=s::vertical_closing(*result,-20.);}}
int stack_impact(int health,double speed,double upper,double lower){return s::impact_damage(health,speed,upper,lower);}
void stack_struck_carrier(double*out){const std::vector<s::Point>points{{0.,0.,0.},{1.,0.,0.},{2.,0.,0.}};
    const std::vector<s::Surface>hits{{{false,1},{0.,0.,0.},{0.,.6,.8}},{{false,1},{1.,0.,0.},{0.,.6,.8}},{{true,2},{2.,0.,0.},{0.,1.,0.}}};
    const auto pressure=s::settled_carrier(points,hits),impact=s::impact_surface(points,hits,-20.);
    out[0]=pressure->carrier.human;out[1]=double(pressure->carrier.id);out[2]=impact->carrier.human;out[3]=double(impact->carrier.id);out[4]=s::vertical_closing(*impact,-20.);
}
}
