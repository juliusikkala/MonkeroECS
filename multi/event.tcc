/*
The MIT License (MIT)

Copyright (c) 2020, 2021, 2022 Julius Ikkala

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#ifndef MONKERO_EVENT_TCC
#define MONKERO_EVENT_TCC
#include "event.hh"
#include "ecs.hh"

namespace monkero
{
event_subscription::event_subscription(ecs* ctx, std::size_t subscription_id)
: ctx(ctx), subscription_id(subscription_id)
{
}

event_subscription::event_subscription(event_subscription&& other)
: ctx(other.ctx), subscription_id(other.subscription_id)
{
    other.ctx = nullptr;
}

event_subscription::~event_subscription()
{
    if(ctx)
        ctx->remove_event_handler(subscription_id);
}

}

#endif
