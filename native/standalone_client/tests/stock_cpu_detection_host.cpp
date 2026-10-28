// Only the CPU/Win32 primitive boundaries are supplied. The complete production
// entry point, native vendor cache/classification and score function execute.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

struct SYSTEM_INFO {
 unsigned char beforeProcessors[20];
 std::uint32_t dwNumberOfProcessors;
 unsigned char afterProcessors[12];
};
static_assert(offsetof(SYSTEM_INFO,dwNumberOfProcessors)==20,"Original Win32 SYSTEM_INFO processor field");
static_assert(sizeof(SYSTEM_INFO)==36,"Original Win32 SYSTEM_INFO width");
std::uint64_t ticks[2];unsigned tickIndex=0,processors;
unsigned cpuidWords[4];std::vector<char> events;
unsigned long long hostReadTicks(){events.push_back('R');assert(tickIndex<2);return ticks[tickIndex++];}
void Sleep(unsigned milliseconds){assert(milliseconds==500);events.push_back('S');}
void GetSystemInfo(SYSTEM_INFO* information){events.push_back('G');std::memset(information,0xa5,sizeof(*information));information->dwNumberOfProcessors=processors;}
void hostCpuid(int* output,int leaf){assert(leaf==0);events.push_back('C');std::memcpy(output,cpuidWords,sizeof(cpuidWords));}
#include "stock_cpu_detection.cpp"
int main(){
 unsigned long long start,end;
 while(std::scanf("%llu %llu %u %u %u %u %u",&start,&end,&processors,&cpuidWords[0],&cpuidWords[1],&cpuidWords[2],&cpuidWords[3])==7)
 {
  ticks[0]=start;ticks[1]=end;tickIndex=0;events.clear();
  const std::int64_t measured=BW::stockMeasureCpuFrequency();assert(tickIndex==2);
  std::printf("%lld ",static_cast<long long>(measured));
  for(char event:events){std::putchar(event);}std::putchar('\n');
 }
}
