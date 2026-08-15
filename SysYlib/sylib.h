#ifndef __SYLIB_H_
#define __SYLIB_H_

#include<stdio.h>
#include<stdarg.h>
#include<sys/time.h>
/* Input & output functions */
int getint(),getch(),getarray(int a[]);
float getfloat();
int getfarray(float a[]);

void putint(int a),putch(int a),putarray(int n,int a[]);
void putfloat(float a);
void putfarray(int n, float a[]);

void putf(char a[], ...);

/* Timing function implementation */
struct timeval _sysy_start,_sysy_end;
#define starttime() _sysy_starttime(__LINE__)
#define stoptime()  _sysy_stoptime(__LINE__)
#define _SYSY_N 1024
int _sysy_l1[_SYSY_N],_sysy_l2[_SYSY_N];
int _sysy_h[_SYSY_N], _sysy_m[_SYSY_N],_sysy_s[_SYSY_N],_sysy_us[_SYSY_N];
int _sysy_idx;
__attribute((constructor)) void before_main(); 
__attribute((destructor)) void after_main();
void _sysy_starttime(int lineno);
void _sysy_stoptime(int lineno);

/* Vector runtime (scalar loop simulation on RV64GC, no V extension) */
void vec_fill(int* a, int v, int n);
void vec_add(int* a, int* b, int* res, int n);
void vec_sub(int* a, int* b, int* res, int n);
void vec_mul(int* a, int* b, int* res, int n);
void vec_scale(int* a, int s, int* res, int n);
int  vec_sum(int* a, int n);

/* Dynamic (variable-length) vector runtime:
 * storage layout on a static bump heap: [ len | e0 e1 ... e(n-1) ],
 * vec_new returns pointer to e0; length is queryable via vec_len. */
int* vec_new(int n);            /* allocate n ints, zero-init, return data ptr */
int  vec_len(int* a);           /* current length of a dynamic vector */
int* vec_resize(int* a, int n); /* reallocate to n ints, keep old data, return new ptr */

#endif
