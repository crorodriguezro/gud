#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int __init gud_init(void)
{
	pr_info("gud: build probe loaded\n");
	return 0;
}

static void __exit gud_exit(void)
{
	pr_info("gud: build probe unloaded\n");
}

module_init(gud_init);
module_exit(gud_exit);

MODULE_DESCRIPTION("OnePlus 6 GUD backport build probe");
MODULE_LICENSE("GPL");
