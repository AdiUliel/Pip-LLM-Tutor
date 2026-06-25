#include <stdio.h>
#include <stdlib.h>
#include "ww_infer.h"
int main(int argc,char**argv){
  static float sig[WW_CLIP], feat[WW_FRAMES*WW_MELS];
  static float b1[48*19*WW_C1_OUT], b2[23*9*WW_C2_OUT], b3[11*4*WW_C3_OUT], prob[WW_NCLASS];
  short pcm[WW_CLIP];
  FILE*fp=fopen(argv[1],"rb"); int n=fread(pcm,2,WW_CLIP,fp); fclose(fp);
  for(int i=0;i<WW_CLIP;i++) sig[i]=(i<n)?pcm[i]/32768.0f:0.0f;
  ww_infer(sig,feat,b1,b2,b3,prob);
  printf("%.6f %.6f %.6f\n",prob[0],prob[1],prob[2]);
  return 0;
}
