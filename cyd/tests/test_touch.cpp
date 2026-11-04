#include "CYDTouchCalibration.h"
#include <cassert>
#include <iostream>
int main() {
 const uint16_t good[8]={200,200,200,3800,3800,200,3800,3800};
 const uint16_t same[8]={500,500,500,500,500,500,500,500};
 const uint16_t bad[8]={65535,0,0,0,0,0,0,0};
 assert(cydCalibrationValid(good));assert(!cydCalibrationValid(same));assert(!cydCalibrationValid(bad));
 for(int i=0;i<4;++i) {assert(cydToolbarButton(i*80,200)==i);assert(cydToolbarButton(i*80+79,239)==i);}
 assert(cydToolbarButton(-1,210)==-1&&cydToolbarButton(320,210)==-1&&cydToolbarButton(20,199)==-1&&cydToolbarButton(20,240)==-1);
 std::cout<<"CYD touch calibration validation and toolbar boundary tests passed\n";
}
