---
comments: true
date: 2011-11-25 22:46:44
layout: post
slug: messing-with-programmers-part-2
title: Messing With Programmers Part 2
wordpress_id: 729


---

If I really hate a particular C programmer, I'll litter some of these statements around `stdio.h` and other headers...


    
    
    #define enum struct
    #define main(x) exit(1)
    #define printf scanf
    
    #define malloc AASDAF
    #define free malloc
    #define AASDAF free
    
    #ifdef STDIO_H_
        #undef STDIO_H_
    #endif
    
    #define int float
    #define double char*
    
    #define while(x) for(;;)
    
    #define FILE maint
    
    #define sizeof(x) (sizeof(x)*2)
    
