#pragma once
#include "platform.h"
namespace gv {
Guide fetch_guide(VideoRef& ref,Cancellation& cancel);
Media fetch_public_media(const Guide& guide,const Part& part,int height,Cancellation& cancel);
}
