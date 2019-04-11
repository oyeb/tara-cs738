#include <cstdlib>
#include <pthread.h>

void assert(bool);

int x = 3;

void* p0(void* y){
  x = *((int *) y) * 2;
  return NULL;
}

int main(){
  pthread_t thr_0;
  int *y = (int*)malloc(sizeof(int));
  pthread_create(&thr_0, NULL, p0, y );
  assert(x == 6);
  pthread_join(thr_0, NULL);
  return x;
}
