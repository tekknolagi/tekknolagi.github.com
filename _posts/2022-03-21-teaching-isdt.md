---
title: Teaching ISDT
layout: post
date: 2022-03-21
co_authors: Tom Hebb
description: Tom and I give a retrospective of our Fall 2021 course
---

Who we are:
* Software engineers
* Tufts alumni
* Friends who lived together

Origin story / why we did this:
* Friend was interviewing someone at work
* I asserted "the kids know the tools"
* Tom asserted "no they don't"
* I suggested we fix that by teaching the tools

It started with an argument. At the end of March 2021, Tom and I had very
different opinions about what students learned in undergrad at Tufts. After a
bit of good-natured bickering, we decided that the kids are not alright. We
decided that we should teach a course.

After several phone calls, 50-email-long threads, and a lot of paperwork, the
Computer Science department offered us jobs teaching *CS 50: Introduction to
Software Development Tooling*.

Now, one semester and some rest later, we are writing up what we learned.

## How we prepared

* Developed a sketch of what we wanted to do, in four modules
  * Overview and motivation
  * Learning objectives
  * Week-by-week bulleted syllabus
  * Overlap and contrast with existing Tufts courses
* Emailed a professor and called him
* Emailed and called some other professors (at Tufts and elsewhere)
  * "Students will spend 4x as much time doing the assignment as you did writing
    it"
* Refined the sketch
* Emailed the department head (50 emails later...)
* Met twice weekly every week for three months, writing lectures and assignments
* Hired three absolutely indispensable TAs

## Goals

To measure how the course went, we have included our overview and learning
objectives.

### Overview

TODO: explain instead of quoting

> Effective software development requires more than just coding skill: in
> industry and academia alike, developers use tools to keep their code
> maintainable and reliable. In this course, you will learn four fundamental
> categories of tooling: the command line, version control, build systems, and
> correctness. We'll dive deep into one industry-standard tool from each
> category via hands-on projects and exploration of existing codebases, then
> survey other tools in the same category and discuss why you might choose one
> over another. By the end of the course, you will have a robust toolset both
> to manage complexity in your future projects and to effectively ramp up on
> software projects you encounter in the real world.

> We are teaching this course as a series of four modules. The first, Command
> Line, will give you an overview of the operating system and tools available
> to you to make your life as a software engineer easier. The second, VCS, will
> teach you about how to version control your software with Git, so that you
> may maintain a history of your changes and collaborate with others. The
> third, Build, will teach you about how to reliably build your software with
> Make. The fourth and final module, Correctness, will introduce you to testing
> and other tools for ensuring that your software meets quality standards.

### Learning objectives

TODO: explain instead of quoting

By the end of this course, we hope that students will be able to do the following:
1. Track changes in their software projects, including developing multiple
   parallel changesets, using Git.
1. Carry out development comfortably on a Linux system by using shell commands
   and OS utilities, including synthesizing multiple utilities together to
   answer novel questions.
1. Write and understand simple Makefiles for building C projects.
1. Write software that is designed to be tested as well as used.
1. Write tests for their software using UTest.
1. Set up continuous integration to run tests on new code changes using GitHub
   Actions.
1. Search for and read documentation for new and unfamiliar tools using `man`
   and the internet.
1. Independently answer questions about unfamiliar systems by reading
   documentation, carrying out experiments, and exploring the underlying source
   code using the tools they've learned.

## How did we do?

Despite meeting so much, we did not finish on time

* Two students dropped
* Two instances of academic misconduct
* Class average of mid-high Bs
* Fell behind on lecture notes for VCS, BLD modules
  * Writing notes and slides takes **forever**
* Tried to avoid writing sample solutions (text and code)
  * Learned that we needed to write sample solutions for grading and sometimes
    also for office hours
* TAs did a good job grading assignments

Student feedback:

* Enjoyed "flavor" in assignments
* Complaints about rubric point allocation
* Complaints about vague homework questions
* Complaints about unreasonable expectations on rubrics
* Complaints about modifying rubrics after looking at submissions

We should hire a TA whose sole responsibility is to be a project manager

## What we enjoyed
