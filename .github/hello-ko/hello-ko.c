// full_phys_dump.c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#define DUMP_FILE "/data/local/tmp/full_phys_dump.bin"
#define PROC_ENTRY "full_phys_dump"

static struct proc_dir_entry *proc_entry;
static struct file *dump_file;
static loff_t file_pos;

static int dump_physical_memory(void)
{
    struct resource *res;
    unsigned long long start, end;
    void __iomem *vaddr;
    char *buffer;
    int ret = 0;
    const size_t chunk_size = 2*1024*1024; // 2MB chunks

    // 获取所有系统物理内存区域
    for (res = iomem_resource.child; res; res = res->sibling) {
        if (strcmp(res->name, "System RAM") != 0)
            continue;

        start = res->start;
        end = res->end;

        printk(KERN_INFO "Dumping physical range: %llx-%llx\n", start, end);

        buffer = vmalloc(chunk_size);
        if (!buffer) {
            ret = -ENOMEM;
            break;
        }

        while (start <= end) {
            unsigned long size = min_t(unsigned long, chunk_size, end - start + 1);
            
            // 映射物理内存
            vaddr = memremap(start, size, MEMREMAP_WB);
            if (!vaddr) {
                printk(KERN_WARNING "Failed to map %llx-%llx\n", start, start+size-1);
                start += size;
                continue;
            }

            // 复制内存内容
            memcpy(buffer, vaddr, size);
            memunmap(vaddr);

            // 写入文件
            if (kernel_write(dump_file, buffer, size, &file_pos) != size) {
                printk(KERN_ERR "Write failed at %llx\n", start);
                ret = -EIO;
                break;
            }

            start += size;
        }

        vfree(buffer);
        if (ret)
            break;
    }

    return ret;
}

static ssize_t proc_write(struct file *file, const char __user *buf,
                        size_t count, loff_t *ppos)
{
    int ret;

    // 打开输出文件
    dump_file = filp_open(DUMP_FILE, O_WRONLY|O_CREAT|O_TRUNC, 0600);
    if (IS_ERR(dump_file)) {
        ret = PTR_ERR(dump_file);
        printk(KERN_ERR "Open file failed: %d\n", ret);
        return ret;
    }

    file_pos = 0;

    ret = dump_physical_memory();
    filp_close(dump_file, NULL);

    return ret ? ret : count;
}

static const struct proc_ops fops = {
    .proc_write = proc_write,
};

static int __init phys_dump_init(void)
{
    proc_entry = proc_create(PROC_ENTRY, 0222, NULL, &fops);
    if (!proc_entry)
        return -ENOMEM;

    printk(KERN_INFO "Physical memory dumper loaded\n");
    return 0;
}

static void __exit phys_dump_exit(void)
{
    if (proc_entry)
        proc_remove(proc_entry);

    printk(KERN_INFO "Physical memory dumper unloaded\n");
}

module_init(phys_dump_init);
module_exit(phys_dump_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Full Physical Memory Dumper");
