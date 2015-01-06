---
layout: post
title: "Creating a virtual machine, part 1"
---
{% include JB/setup %}

For my English project last year, I tasked myself with creating a virtual machine (yes, English &mdash; as part of the class, we designed and executed "20 percent projects"). My current virtual machine, [Carp](http://github.com/tekknolagi/carp), is either an over-complicated or over-simplified virtual machine, depending on how you look at it. It has a lot of instructions and the process of executing those instructions is complex. However, the code is (read: should be) reasonably easy to understand if you know C.

In this series, I plan on creating a virtual machine in C, ground up. I'll provide all the code from each post in a GitHub repository, separated into branches. That way, you can clone it and build from whatever point you choose. In this post, I'll create the foundation:

* Registers
* Skeleton of the evaluation loop

So. What is a register? On a real machine, registers are small amounts of storage very close to the processor. The processor can read from and write to the registers very quickly. Thus, they are used for storing the operands and results for atomic operations (like add, mul, etc). In our machine, they serve a similar purpose, but lack physical proximity (because everything is virtualized).

As far as the evaluation loop goes... our machine will only execute one instruction at a time, and execute those instructions sequentially. We will use a simple while loop.

Let's build our machine. Since we are likely going to want to keep all of our machine state around at once, we need an easy way to pass it (or a pointer to it) to functions... like a struct!

{% highlight c %}
typedef struct {
  uint32_t ip, sp, fp, ra, zero; // ra, zero stolen from MIPS
} vm_state_t;
{% endhighlight %}

While this is nice and simple, it could be frustrating to programmatically access different registers from interpreted code. Putting all of the registers in an array makes it a bit simpler:

{% highlight c %}
enum {
  // ra, zero stolen from MIPS
  // NUM_REGS counts the number of registers in case we need to iterate
  REG_UNDEF = -1, REG_IP, REG_SP, REG_FP, REG_RA, REG_ZERO,
  NUM_REGS
};

typedef struct {
  uint32_t regs[REG_NUM];
} vm_state_t;
{% endhighlight %}

Ah, much better. Now we can access a register through something like `regs[REG_SP]`. Onto the simple evaluation loop:

{% highlight c %}
bool vm_execute (vm_t *vm, vm_word code[]) {
  while (vm->running) {
    switch (code[vm->regs[REG_IP]]) {
    case 0: // HALT
      vm->running = false;
      break;
    case 1: // Print "Hello, world!\n"
      printf("Hello, world!\n");
      break;
    default:
      break;
    }

    vm->regs[REG_IP]++;
  }

  return true;
}
{% endhighlight %}

This is basically all your evaluation loop needs to be. It takes a vm struct and your code in an array and then goes through it, executing the instruction where the instruction pointer points. Because, you know, that's what an instruction pointer does.

Right now we have only defined two instructions &mdash; and that's okay! More will come later.

In the next post, we'll add a stack and a couple more instructions.
