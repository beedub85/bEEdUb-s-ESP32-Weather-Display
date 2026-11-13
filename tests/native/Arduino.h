#pragma once
#include <string>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
class String:public std::string {
public:
 using std::string::string;
 String()=default;String(const std::string&s):std::string(s){}
 String(float n,int p=2){char b[64];snprintf(b,sizeof(b),"%.*f",p,n);assign(b);}
 String(double n,int p=2):String((float)n,p){}
 String(int n):std::string(std::to_string(n)){}String(unsigned n):std::string(std::to_string(n)){}
 String(long n):std::string(std::to_string(n)){}String(unsigned long n):std::string(std::to_string(n)){}
 bool startsWith(const char*s)const{return rfind(s,0)==0;}
 bool endsWith(const char*s)const {std::string v=s;return size()>=v.size() && compare(size()-v.size(),v.size(),v)==0;}
 void trim(){auto a=find_first_not_of(" \t\r\n");if(a==npos){clear();return;}assign(substr(a,find_last_not_of(" \t\r\n")-a+1));}
 void toLowerCase(){for(char &c : *this) if(c>='A' && c<='Z') c += 'a'-'A';}
 bool isEmpty()const{return empty();}
};
inline String operator+(const String&s,int n){return String(static_cast<const std::string&>(s)+std::to_string(n));}
extern uint32_t testMillis;
inline uint32_t millis(){return testMillis;}
