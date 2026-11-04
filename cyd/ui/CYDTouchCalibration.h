#pragma once
#include <stdint.h>
inline bool cydCalibrationValid(const uint16_t *p) {
 uint16_t lo[2]={4095,4095},hi[2]={0,0};
 for(unsigned i=0;i<8;++i) {
  if(p[i]>4095)return false;
  if(p[i]<lo[i%2])lo[i%2]=p[i];
  if(p[i]>hi[i%2])hi[i%2]=p[i];
 }
 return hi[0]-lo[0]>800 && hi[1]-lo[1]>800;
}

inline int cydToolbarButton(int x,int y) {
 return x>=0&&x<320&&y>=200&&y<240?x/80:-1;
}
void cydToolbarTap(int16_t x,int16_t y);
