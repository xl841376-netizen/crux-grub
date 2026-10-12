/*
 * [crux] GRUB 侧的显示交接模块
 *
 * 目的：在 GRUB 把控制权交给内核之前，把 DSI/DPU 的显示电路
 * 按"内核自己那套"重放一遍（录制来自内核驱动的初始化），
 * 内核侧驱动 probe 时校验，对上就直接用。
 *
 * 旁路：结果写进 GOP 帧缓冲尾部的信箱，开机后用
 *       /sys/kernel/debug/crux_te_sync/gop 按偏移读回来核对。
 */

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/command.h>
#include <grub/env.h>
#include <grub/file.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

/* [crux] 交接时一个字符都不许往屏幕上打（会把切换现场盖住）。
 * 所有证据走 GOP 信箱，内核侧 /sys/kernel/debug/crux_te_sync/gop 读。 */
#define grub_printf(...)	do { } while (0)

#define DSI0_BASE	0x0AE94000UL
#define INTF1_BASE	0x0AE6B800UL
/* DSI0 + 0x1f8 是内核从不碰的 scratch */
#define DSI_SCR1_OFF	0x1f8UL
#define CRUX_HANDOVER_MAGIC	0x43525558UL	/* "CRUX" */

/* 信箱：GOP 区 36MB 里的最后 4MB，肯定不在可见画面内 */
#define GOP_MAILBOX	0x9C000000UL
#define GOP_MBOX_OFF	0x2000000UL
#define MBOX_MAGIC	0
#define MBOX_VER	1
#define MBOX_CTRL	2
#define MBOX_TRIG	3
#define MBOX_CFG0	4
#define MBOX_DONE	5
#define MBOX_DONE_MAGIC	0x444F4E45UL	/* "DONE" */
#define MBOX_STOP	6
#define MBOX_STOP_MAGIC	0x53544F50UL	/* "STOP" */
#define MBOX_TRIG_AFTER	7
#define MBOX_CTRL_AFTER	8

struct crux_reg {
	const char *name;
	unsigned long off;
};

/* dsi.xml.h 里的偏移 */
static const struct crux_reg dsi_regs[] = {
	{ "DSI_CTRL",		0x000 },
	{ "DSI_VID_CFG0",	0x00c },
	{ "DSI_CMD_DMA_CTRL",	0x038 },
	{ "DSI_CMD_CFG0",	0x03c },
	{ "DSI_CMD_CFG1",	0x040 },
	{ "DSI_TRIG_CTRL",	0x080 },
	{ "DSI_LANE_CTRL",	0x0a8 },
	{ "DSI_CLKOUT_TIM",	0x0c0 },
	{ "DSI_ERR_INT_MASK0",	0x108 },
	{ "DSI_INTR_CTRL",	0x10c },
	{ "DSI_RESET",		0x114 },
	{ "DSI_CLK_CTRL",	0x118 },
	{ "DSI_PHY_RESET",	0x128 },
	{ "DSI_T_CLK_PRE",	0x17c },
	{ "DSI_MDP_CTRL2",	0x1b4 },
	{ "DSI_VERSION",	0x1f0 },
};

static inline grub_uint32_t crux_rd (unsigned long a)
{
	return *(volatile grub_uint32_t *) a;
}

static inline void crux_wr (unsigned long a, grub_uint32_t v)
{
	*(volatile grub_uint32_t *) a = v;
}

static void mbox_put (int idx, grub_uint32_t v)
{
	crux_wr (GOP_MAILBOX + GOP_MBOX_OFF + idx * 4, v);
}

/* 把 GRUB 阶段看到的整片 DSI 寄存器抄进信箱（idx 从 8 开始） */
static void mbox_dump_regs (void)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE (dsi_regs); i++)
		mbox_put (8 + i, crux_rd (DSI0_BASE + dsi_regs[i].off));
}

static grub_err_t
cmd_crux_regs (grub_command_t cmd __attribute__ ((unused)),
	       int argc __attribute__ ((unused)),
	       char **argv __attribute__ ((unused)))
{
	unsigned i;

	grub_printf ("    [crux] DSI0 @%08lx\n", DSI0_BASE);
	for (i = 0; i < ARRAY_SIZE (dsi_regs); i++)
		grub_printf ("    %-18s %08x\n", dsi_regs[i].name,
			     crux_rd (DSI0_BASE + dsi_regs[i].off));
	return GRUB_ERR_NONE;
}

struct crux_wr {
	const char *name;
	unsigned long addr;
	grub_uint32_t val;
	grub_uint32_t mask;
	char grp;		/* 'C' 配置类 / 'S' DMA、流、复位类 */
};

/* 预置表：下面这一段由 gen_replay_tab.py 从内核录制里生成 */
/*
 * 面板 DCS 初始化序列（32 包，内核录制里抄出来的，暂不重放：
 * 引导器已经把这个面板初始化好，交接时保持它就行）
 * D   3  f0 5a 5a
 * D   3  fc 5a 5a
 * D   2  b0 0c
 * D   2  ff 10
 * D   2  b0 2f
 * D   2  d1 01
 * D   3  f0 a5 a5
 * D   3  fc a5 a5
 * D   1  11
 * D   3  f0 5a 5a
 * D   2  35 01
 * D   3  b7 01 4b
 * D   2  b0 06
 * D   2  b7 10
 * D   3  f0 a5 a5
 * D   5  2a 00 00 04 37
 * D   5  2b 00 00 09 23
 * D   3  f0 5a 5a
 * D   3  fc 5a 5a
 * D   2  b0 23
 * D   2  d1 16
 * D  12  e9 11 55 a6 75 a3 a9 eb 71 00 1a b8
 * D  12  d9 14 00 00 8f 6e 00 00 8f 2e 6e 34
 * D 109  c0 31 01 03 00 06 00 00 00 00 00 00 00 00 00 00 00 00 50 00 00 40 3d 00 5e 00 00 00 20 07 20 30 58 00 00 00 00 00 00 e0 0b 75 d8 00 00 00 00 fc 00 00 00 f6 97 0f 6d e6 cd 05 21 8b ec ed ed ed
 * D   2  c0 31
 * D   7  e1 00 00 02 02 42 02
 * D   7  e2 00 00 00 00 00 00
 * D   2  b0 0c
 * D   2  e1 19
 * D   3  f0 a5 a5
 * D   3  fc a5 a5
 * D   2  53 20
 */
static const struct crux_wr crux_init_tab[] = {
	{ "R000", DSI0_BASE + 0x000, 0x000001f5, 0xffffffff, 'S' },
	{ "R038", DSI0_BASE + 0x038, 0x14000000, 0xffffffff, 'S' },
	{ "R03c", DSI0_BASE + 0x03c, 0x00000008, 0xffffffff, 'C' },
	{ "R040", DSI0_BASE + 0x040, 0x00013c2c, 0xffffffff, 'C' },
	{ "R044", DSI0_BASE + 0x044, 0x00001000, 0xffffffff, 'S' },
	{ "R048", DSI0_BASE + 0x048, 0x00000004, 0xffffffff, 'S' },
	{ "R054", DSI0_BASE + 0x054, 0x0ca90039, 0xffffffff, 'S' },
	{ "R058", DSI0_BASE + 0x058, 0x09240438, 0xffffffff, 'S' },
	{ "R080", DSI0_BASE + 0x080, 0x80001004, 0xffffffff, 'C' },
	{ "R08c", DSI0_BASE + 0x08c, 0x00000001, 0xffffffff, 'S' },
	{ "R0ac", DSI0_BASE + 0x0ac, 0x00000000, 0xffffffff, 'S' },
	{ "R0c0", DSI0_BASE + 0x0c0, 0x00001921, 0xffffffff, 'C' },
	{ "R0c8", DSI0_BASE + 0x0c8, 0x00000001, 0xffffffff, 'S' },
	{ "R108", DSI0_BASE + 0x108, 0x13ff3fe0, 0xffffffff, 'S' },
	{ "R10c", DSI0_BASE + 0x10c, 0x02000700, 0xffffffff, 'S' },
	{ "R114", DSI0_BASE + 0x114, 0x00000000, 0xffffffff, 'S' },
	{ "R118", DSI0_BASE + 0x118, 0x0000023f, 0xffffffff, 'C' },
	{ "R128", DSI0_BASE + 0x128, 0x00000000, 0xffffffff, 'S' },
	{ "R1b4", DSI0_BASE + 0x1b4, 0x00010006, 0xffffffff, 'C' },
	{ NULL, 0, 0, 0, 0 },
};

/* [crux] 四档分档表：四档之间 DSI 侧只差 R0c0（DSI_CLKOUT_TIM 一类）。
 * 用哪一档由设备树面板节点的 samsung,default-framerate 决定（crux_dtb_rate），
 * 与内核面板驱动同一份真源；读不到就用 60。
 */
static const struct {
	grub_uint32_t fps;
	grub_uint32_t r0c0;
} crux_mode_tab[] = {
	{ 60, 0x00001921 },
	{ 72, 0x00001a23 },
	{ 81, 0x00001b27 },
	{ 90, 0x00001b2b },
};

static grub_uint32_t crux_r0c0 = 0x00001921;
/* [crux] 表项地址的手工修正量：set crux_dsi_off=N（默认 0） */
static int crux_off_extra;
#define MBOX_MODE	9
#define MBOX_DTB_RATE	10

/* [crux] 从设备树里取帧率默认档。
 * 来源与内核面板驱动同一份设备树（面板节点 samsung,ea8076-f1s 的
 * samsung,default-framerate），所以引导器和内核不会选到不同的表。
 * 读不到就返回 0，调用方退回 60。
 */
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP	4
#define FDT_END	9

static const char *crux_dtb_compat = "samsung,ea8076-f1s";
static const char *crux_dtb_prop = "samsung,default-framerate";

static int
crux_has_substr (const char *hay, unsigned int len, const char *needle)
{
	unsigned int i, nl = grub_strlen (needle);

	if (!nl || len < nl)
		return 0;
	for (i = 0; i + nl <= len; i++)
		if (!grub_memcmp (hay + i, needle, nl))
			return 1;
	return 0;
}

static grub_uint32_t
crux_dtb_rate (const char *path)
{
	grub_file_t f;
	grub_uint8_t *blob;
	grub_uint32_t sz, off, off_struct, off_strings, rate = 0;
	int compat_hit = 0;

	if (!path)
		return 0;
	f = grub_file_open (path, GRUB_FILE_TYPE_DEVICE_TREE_IMAGE);
	if (!f)
		return 0;
	sz = (grub_uint32_t) grub_file_size (f);
	if (!sz || sz > (1 << 20)) {
		grub_file_close (f);
		return 0;
	}
	blob = grub_malloc (sz);
	if (!blob) {
		grub_file_close (f);
		return 0;
	}
	if (grub_file_read (f, blob, sz) != (grub_ssize_t) sz) {
		grub_free (blob);
		grub_file_close (f);
		return 0;
	}
	grub_file_close (f);
	grub_errno = GRUB_ERR_NONE;

	if (grub_be_to_cpu32 (((grub_uint32_t *) blob)[0]) != 0xd00dfeed) {
		grub_free (blob);
		return 0;
	}
	off_struct = grub_be_to_cpu32 (((grub_uint32_t *) blob)[2]);
	off_strings = grub_be_to_cpu32 (((grub_uint32_t *) blob)[3]);
	off = off_struct;

	while (off + 4 <= sz) {
		grub_uint32_t tok = grub_be_to_cpu32 (*(grub_uint32_t *) (blob + off));

		off += 4;
		if (tok == FDT_PROP) {
			grub_uint32_t len, nameoff;
			const char *pname;
			const grub_uint8_t *pval;

			len = grub_be_to_cpu32 (*(grub_uint32_t *) (blob + off));
			nameoff = grub_be_to_cpu32 (*(grub_uint32_t *) (blob + off + 4));
			pname = (const char *) blob + off_strings + nameoff;
			pval = blob + off + 8;
			off += 8 + ((len + 3) & ~3U);

			if (!grub_strcmp (pname, "compatible") &&
			    crux_has_substr ((const char *) pval, len, crux_dtb_compat))
				compat_hit = 1;
			else if (!grub_strcmp (pname, crux_dtb_prop) && len >= 4)
				rate = grub_be_to_cpu32 (*(grub_uint32_t *) pval);
		} else if (tok == FDT_BEGIN_NODE) {
			const char *nm = (const char *) blob + off;

			off += (grub_strlen (nm) + 1 + 3) & ~3U;
			compat_hit = 0;
		} else if (tok == FDT_END_NODE) {
			if (compat_hit && rate)
				break;
		} else if (tok == FDT_END) {
			break;
		}
	}

	grub_free (blob);
	return rate;
}

static grub_err_t
cmd_crux_display_init (grub_command_t cmd __attribute__ ((unused)),
		       int argc, char **argv)
{
	unsigned i;
	int only_cfg = 0;
	grub_uint32_t ver;
	grub_uint32_t ctrl, trig, cfg0;

	/* 先写信箱和交接标记：不管后面成不成，都要留下痕迹 */
	ver = crux_rd (DSI0_BASE + 0x1f0);
	mbox_put (MBOX_MAGIC, CRUX_HANDOVER_MAGIC);
	mbox_put (MBOX_VER, ver);
	mbox_dump_regs ();
	mbox_put (MBOX_CTRL, crux_rd (DSI0_BASE + 0x000));
	mbox_put (MBOX_TRIG, crux_rd (DSI0_BASE + 0x080));
	mbox_put (MBOX_CFG0, crux_rd (DSI0_BASE + 0x03c));

	crux_wr (DSI0_BASE + DSI_SCR1_OFF, CRUX_HANDOVER_MAGIC);

	/* 安全闸：这块要是全 0，说明没上电，别动 */
	if (crux_rd (DSI0_BASE + 0x03c) == 0 && crux_rd (DSI0_BASE + 0x080) == 0) {
		grub_printf ("    [crux] DSI 寄存器全 0，跳过预置\n");
		return GRUB_ERR_NONE;
	}

	/* 交接：可选停帧（grub.cfg 里 set crux_stop=1）。默认不动：这块面板的接收端被中途掐断后会挂死 */
	if (grub_env_get ("crux_stop") && grub_env_get ("crux_stop")[0] == '1')
	  {
		crux_wr (DSI0_BASE + 0x080, 0);	/* TRIG_CTRL = 0 */
		crux_wr (DSI0_BASE + 0x038, 0);	/* CMD_DMA_CTRL = 0 */
		mbox_put (MBOX_TRIG_AFTER, crux_rd (DSI0_BASE + 0x080));
		mbox_put (MBOX_STOP, MBOX_STOP_MAGIC);
		grub_printf ("    [crux] 送帧已停\n");
	  }

	/* A/B 开关：crux_replay=1 全重放；=2 只重放配置类（跳过 DMA/流/复位）；不设则一条不写 */
	{
		const char *rp = grub_env_get ("crux_replay");

		if (!rp || (rp[0] != '1' && rp[0] != '2')) {
			grub_printf ("    [crux] 不重放配置（要重放就 set crux_replay=1 或 2）\n");
			mbox_put (MBOX_DONE, MBOX_DONE_MAGIC);
			return GRUB_ERR_NONE;
		}
		only_cfg = (rp[0] == '2');
		grub_printf ("    [crux] 重放模式 %s\n",
			     only_cfg ? "2（只配置类）" : "1（全部）");
	}

	/* 选档：默认档来自设备树面板节点的 samsung,default-framerate
	 * （路径由调用方传入，与 devicetree 命令用同一个文件）。读不到就是 60。 */
	/* [crux] 手工修正量（默认 0）：set crux_dsi_off=N */
	{
		const char *oe = grub_env_get ("crux_dsi_off");
		if (oe)
			crux_off_extra = (int) grub_strtoul (oe, 0, 0);
	}
	{
		grub_uint32_t fps = crux_dtb_rate (argc > 0 ? argv[0] : NULL);
		unsigned m;

		if (!fps)
			fps = 60;
		mbox_put (MBOX_DTB_RATE, fps);
		for (m = 0; m < ARRAY_SIZE (crux_mode_tab); m++)
			if (crux_mode_tab[m].fps == fps)
				crux_r0c0 = crux_mode_tab[m].r0c0;
		mbox_put (MBOX_MODE, crux_r0c0);
	}

	for (i = 0; i < ARRAY_SIZE (crux_init_tab); i++) {
		grub_uint32_t old, new, val;

		if (!crux_init_tab[i].name)
			continue;
		if (only_cfg && crux_init_tab[i].grp != 'C')
			continue;
		old = crux_rd (crux_init_tab[i].addr + crux_off_extra);
		val = crux_init_tab[i].val;
		if (!grub_strcmp (crux_init_tab[i].name, "R0c0"))
			val = crux_r0c0;
		new = (old & ~crux_init_tab[i].mask) |
		      (val & crux_init_tab[i].mask);
		crux_wr (crux_init_tab[i].addr + crux_off_extra, new);
		if (old != crux_init_tab[i].val)
			grub_printf ("    [crux] %-6s %08x -> %08x\n",
				     crux_init_tab[i].name, old,
				     crux_rd (crux_init_tab[i].addr + crux_off_extra));
	}

	/* 重放完把结果写回信箱 */
	ctrl = crux_rd (DSI0_BASE + 0x000);
	trig = crux_rd (DSI0_BASE + 0x080);
	cfg0 = crux_rd (DSI0_BASE + 0x03c);
	mbox_put (MBOX_CTRL, ctrl);
	mbox_put (MBOX_TRIG, trig);
	mbox_put (MBOX_CFG0, cfg0);
	/* [crux] 留痕：表里记录过、内核不常重写的三项（R040 / R054 / R1b4）
	 * 如果引导器写入命中正确地址，下面的读回值应分别等于
	 *   0x00013c2c / 0x0ca90039 / 0x00010006
	 */
	mbox_put (32, crux_rd (DSI0_BASE + 0x040));
	mbox_put (33, crux_rd (DSI0_BASE + 0x054));
	mbox_put (34, crux_rd (DSI0_BASE + 0x1b4));
	mbox_put (MBOX_DONE, MBOX_DONE_MAGIC);
	grub_printf ("    [crux] 重放完 CTRL=%08x TRIG=%08x CFG0=%08x\n",
		     ctrl, trig, cfg0);
	return GRUB_ERR_NONE;
}

static grub_command_t cmd_regs, cmd_init;

GRUB_MOD_INIT (cruxdisplay)
{
	cmd_regs = grub_register_command ("crux_regs", cmd_crux_regs,
					  0, N_("Dump crux display registers."));
	cmd_init = grub_register_command ("crux_display_init", cmd_crux_display_init,
					  N_("DEVICETREE"), N_("Pre-init crux display for handover."));
}

GRUB_MOD_FINI (cruxdisplay)
{
	grub_unregister_command (cmd_regs);
	grub_unregister_command (cmd_init);
}