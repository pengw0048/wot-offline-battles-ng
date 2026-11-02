#include "stock_native_projectile_cursor.hpp"
#include "stock_native_projectile_history.hpp"
#include <cassert>
#include <cmath>
#include <stdexcept>
using namespace stock_native_projectile;
namespace {
Cursor shot(const char* id){Cursor s;s.key={id,0};s.velocity={10,0,0};s.max_time=2;s.max_distance=100;return s;}
struct Sink:CursorSink{
    Cursors* manager=nullptr;bool mutate=false;std::vector<std::string> calls,terminals;
    std::optional<Terminal> chord(const Cursor&s,const Chord&)override{
        calls.push_back(s.key.projectile);
        if(s.key.projectile=="bad")throw std::runtime_error("local failure");
        if(mutate&&s.key.projectile=="first"){
            auto next=shot("later");assert(manager->launch(next));assert(manager->remove(s.key));assert(!manager->remove(s.key));
            assert(!manager->reset());assert(!manager->restore(next));
        }
        return {};
    }
    void terminal(const Cursor&s,const Terminal&t)override{terminals.push_back(s.key.projectile+":"+t.reason);}
};
}
int main(){
    Cursors manager;Sink sink;sink.manager=&manager;
    assert(manager.launch(shot("one")));assert(manager.launch(shot("two")));assert(!manager.launch(shot("one")));
    assert(manager.advance(.2,sink,1));assert(manager.get({"one",0})->cursor_time==.05);assert(manager.get({"two",0})->cursor_time==0);
    assert(manager.advance(.2,sink,1));assert(manager.get({"two",0})->cursor_time==.05);
    assert(manager.advance(.2,sink,6));assert(manager.metrics().chords==6);assert(manager.metrics().debt_after<1e-12);
    auto detached=manager.snapshot();detached[0].position[0]=999;assert(manager.get({"one",0})->position[0]==2);
    assert(!manager.advance(.1,sink));assert(manager.remove({"one",0}));assert(!manager.launch(shot("one")));
    assert(manager.rollback({"one",0}));assert(manager.launch(shot("one")));
    auto replacement=shot("canonical");replacement.cursor_time=.1;replacement.distance=5;replacement.position={99,99,99};
    assert(manager.replace(replacement,Key{"one",0}));assert(manager.get({"canonical",0})->position[0]==1);
    replacement.cursor_time=3;assert(!manager.replace(replacement));assert(manager.get({"canonical",0})->cursor_time==.1);
    assert(manager.reset());assert(manager.size()==0);assert(manager.now()==.2);
    assert(manager.launch(shot("bad")));assert(manager.launch(shot("good")));assert(manager.advance(.3,sink));
    assert(!manager.contains({"bad",0}));assert(manager.get({"good",0})->cursor_time==.3);
    assert(sink.terminals.back()=="bad:callback_error");
    Cursors reentrant;Sink mutation;mutation.manager=&reentrant;mutation.mutate=true;
    assert(reentrant.launch(shot("first")));assert(reentrant.advance(.2,mutation,4));assert(!reentrant.contains({"first",0}));assert(reentrant.contains({"later",0}));assert(reentrant.get({"later",0})->cursor_time==0);
    auto range=shot("range");range.max_distance=.25;assert(reentrant.launch(range));assert(reentrant.advance(.25,mutation));
    assert(!reentrant.contains({"range",0}));assert(mutation.terminals.back()=="range:max_distance");
    History history;Pose a;a.yaw=3.1;Pose b;b.yaw=-3.1;b.position={20,0,0};b.siege=2;
    history.sample(0,{{"x",a}});history.sample(1,{{"x",b}});
    assert(!history.at("x",-1));assert(!history.at("x",2));assert(!history.at("unknown",.5));
    auto midpoint=history.at("x",.5);assert(midpoint&&midpoint->siege==0&&midpoint->position[0]==10&&std::abs(midpoint->yaw-3.141592653589793)<1e-12);
    assert(history.at("x",1)->siege==2);assert(history.sweep("x",0,1)->size()<=17);
    b.position={40,0,0};history.sample(1,{{"x",b}});assert(history.samples().size()==2);assert(history.at("x",1)->position[0]==40);
    history.sample(25,{{"x",b}});assert(history.samples().size()==2);
    {
        // A hydraulic body belongs to the same sample and interpolation
        // fraction as the baseline; no current native pose may replace it.
        History body_history;Pose start,end;
        start.require_hydraulic_body=end.require_hydraulic_body=true;
        start.hydraulic_body=std::array<double,8>{1,2,3,3.1,.1,.2,3.1,-.1};
        end.position={20,0,0};end.siege=2;
        end.hydraulic_body=std::array<double,8>{21,6,7,-3.1,.5,.6,-3.1,.3};
        body_history.sample(0,{{"human",start}});body_history.sample(1,{{"human",end}});
        auto mid=body_history.at("human",.5);assert(mid&&mid->position[0]==10&&mid->siege==0);
        assert(mid->body_available(true)&&mid->hydraulic_body);
        const auto&body=*mid->hydraulic_body;
        assert(body[0]==11&&body[1]==4&&body[2]==5);
        assert(std::abs(body[3]-3.141592653589793)<1e-12);
        assert(std::abs(body[4]-.3)<1e-12&&std::abs(body[5]-.4)<1e-12);
        assert(std::abs(body[6]-3.141592653589793)<1e-12&&std::abs(body[7]-.1)<1e-12);
        assert(body_history.sweep("human",0,1)->size()>2);
        // A missing endpoint invalidates this receipt, including a same-time
        // sample replacement. It must not carry the earlier body forward.
        end.hydraulic_body.reset();body_history.sample(1,{{"human",end}});
        mid=body_history.at("human",.5);assert(mid&&!mid->hydraulic_body);
        assert(!mid->body_available(true)&&mid->body_available(false));
        start.hydraulic_body.reset();end.hydraulic_body=std::array<double,8>{21,6,7,0,.5,.6,0,.3};
        body_history.sample(2,{{"human",start}});body_history.sample(3,{{"human",end}});
        assert(!body_history.at("human",2.5)->hydraulic_body);
        // Ordinary Humans need no receipt. Canonical Bots retain their own
        // existing hydraulic owner rather than requiring a Human wire field.
        Pose bot;assert(bot.body_available(true)&&bot.body_available(false));
        assert(start.body_available(false)&&!start.body_available(true));
    }
    return 0;
}
