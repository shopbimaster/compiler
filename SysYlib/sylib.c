#include<stdio.h>
#include<stdarg.h>
#include<sys/time.h>
#include"sylib.h"
/* Input & output functions */
int getint(){int t; scanf("%d",&t); return t; }
int getch(){char c; scanf("%c",&c); return (int)c; }
float getfloat(){
    float n;
    scanf("%a", &n);
    return n;
}

int getarray(int a[]){
  int n;
  scanf("%d",&n);
  for(int i=0;i<n;i++)scanf("%d",&a[i]);
  return n;
}

int getfarray(float a[]) {
    int n;
    scanf("%d", &n);
    for (int i = 0; i < n; i++) {
        scanf("%a", &a[i]);
    }
    return n;
}
void putint(int a){ printf("%d",a);}
void putch(int a){ printf("%c",a); }
void putarray(int n,int a[]){
  printf("%d:",n);
  for(int i=0;i<n;i++)printf(" %d",a[i]);
  printf("\n");
}
void putfloat(float a) {
  printf("%a", a);
}
void putfarray(int n, float a[]) {
    printf("%d:", n);
    for (int i = 0; i < n; i++) {
        printf(" %a", a[i]);
    }
    printf("\n");
}

void putf(char a[], ...) {
    va_list args;
    va_start(args, a);
    vfprintf(stdout, a, args);
    va_end(args);
}

/* Timing function implementation */
__attribute((constructor)) void before_main(){
  for(int i=0;i<_SYSY_N;i++)
    _sysy_h[i] = _sysy_m[i]= _sysy_s[i] = _sysy_us[i] =0;
  _sysy_idx=1;
}  
__attribute((destructor)) void after_main(){
  for(int i=1;i<_sysy_idx;i++){
    fprintf(stderr,"Timer@%04d-%04d: %dH-%dM-%dS-%dus\n",\
      _sysy_l1[i],_sysy_l2[i],_sysy_h[i],_sysy_m[i],_sysy_s[i],_sysy_us[i]);
    _sysy_us[0]+= _sysy_us[i]; 
    _sysy_s[0] += _sysy_s[i]; _sysy_us[0] %= 1000000;
    _sysy_m[0] += _sysy_m[i]; _sysy_s[0] %= 60;
    _sysy_h[0] += _sysy_h[i]; _sysy_m[0] %= 60;
  }
  fprintf(stderr,"TOTAL: %dH-%dM-%dS-%dus\n",_sysy_h[0],_sysy_m[0],_sysy_s[0],_sysy_us[0]);
}  
void _sysy_starttime(int lineno){
  _sysy_l1[_sysy_idx] = lineno;
  gettimeofday(&_sysy_start,NULL);
}
void _sysy_stoptime(int lineno){
  gettimeofday(&_sysy_end,NULL);
  _sysy_l2[_sysy_idx] = lineno;
  _sysy_us[_sysy_idx] += 1000000 * ( _sysy_end.tv_sec - _sysy_start.tv_sec ) + _sysy_end.tv_usec - _sysy_start.tv_usec;
  _sysy_idx += 1;
}

// Wrappers for SysY programs that call starttime()/stoptime() directly
#undef starttime
#undef stoptime
void starttime() { _sysy_starttime(0); }
void stoptime() { _sysy_stoptime(0); }

/* ===== Vector runtime: scalar loop simulation (RV64GC has no V extension) ===== */
void vec_fill(int* a, int v, int n) {
    for (int i = 0; i < n; i++) a[i] = v;
}
void vec_add(int* a, int* b, int* res, int n) {
    for (int i = 0; i < n; i++) res[i] = a[i] + b[i];
}
void vec_sub(int* a, int* b, int* res, int n) {
    for (int i = 0; i < n; i++) res[i] = a[i] - b[i];
}
void vec_mul(int* a, int* b, int* res, int n) {
    for (int i = 0; i < n; i++) res[i] = a[i] * b[i];
}
void vec_scale(int* a, int s, int* res, int n) {
    for (int i = 0; i < n; i++) res[i] = a[i] * s;
}
int vec_sum(int* a, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}

/* ===== Dynamic (variable-length) vector runtime =====
 * BOOM has no OS brk/malloc, so we keep a static bump heap.
 * Layout per block: [ len | e0 e1 ... e(n-1) ]; vec_new returns &e0.
 * Bump allocator never frees: only grows. Safe for contest use.
 * Heap size configurable; overflow returns 0 (caller must check). */
#define VEC_HEAP_INTS (4 * 1024 * 1024)   /* 16 MB of int heap */
static int _vec_heap[VEC_HEAP_INTS];
static int _vec_off = 0;                   /* next free index (in ints) */

int* vec_new(int n) {
    if (n < 0) n = 0;
    /* need 1 header int + n data ints */
    if (_vec_off + 1 + n > VEC_HEAP_INTS) return 0;   /* heap exhausted */
    int* blk = &_vec_heap[_vec_off];
    blk[0] = n;                                        /* header: length */
    for (int i = 0; i < n; i++) blk[1 + i] = 0;        /* zero-init data */
    _vec_off += 1 + n;
    return &blk[1];                                    /* data pointer */
}

int vec_len(int* a) {
    if (a == 0) return 0;
    return a[-1];                                      /* header is 1 int before data */
}

int* vec_resize(int* a, int n) {
    if (n < 0) n = 0;
    int oldlen = vec_len(a);
    int* nd = vec_new(n);                              /* zero-inits new block */
    if (nd == 0) return 0;
    int cp = (oldlen < n) ? oldlen : n;                /* keep min(old,new) */
    if (a != 0) {
        for (int i = 0; i < cp; i++) nd[i] = a[i];
    }
    return nd;
}
