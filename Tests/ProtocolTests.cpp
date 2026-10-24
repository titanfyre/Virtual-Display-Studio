#include "Protocol.h"
#include "Recovery.h"
#include <iostream>
int main(){
    using namespace DisplayProtocol;unsigned checks=0;
    auto require=[&](bool value){++checks;if(!value){std::cerr<<"Protocol invariant failed\n";std::exit(1);}};
    Request r;require(Valid(r));for(unsigned i=0;i<100000;++i){r.action=static_cast<Action>(i);require(Valid(r)==(i<=2));}
    r=Request{};r.reserved=1;require(!Valid(r));r=Request{};r.version=2;require(!Valid(r));r=Request{};r.size=0;require(!Valid(r));
    State s;require(Valid(s));s.width=3840;require(!Valid(s));s=State{};s.owned=2;require(!Valid(s));
    DisplayFailureBudget budget;for(unsigned i=0;i<4;++i)require(!budget.Fail(100+i));require(budget.Fail(104));require(budget.Fail(1000000));
    budget={};require(!budget.Fail(100));require(!budget.Fail(60200));require(budget.failures==1);require(!budget.Fail(1));require(budget.failures==1);
    std::cout<<checks<<" protocol validation checks passed; no driver access.\n";return 0;
}
