#pragma once
/*
noncopyable被继承了以后，派生类对象可以正常的构造和析构，但是派生类对象不能拷贝和赋值构造
*/

class noncopyable {
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;
protected:
    noncopyable() = default;
    ~noncopyable() = default;
};