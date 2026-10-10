/* SPDX-License-Identifier: MIT */
/*
 * rsxkit — subida de fragment programs a la memoria del RSX.
 *
 * cgcomp (PSL1GHT f649a08, fpparser.cpp: IsPCDisablingInstruction) marca las
 * instrucciones MUL, DP2/DP3/DP4, DIV y NRM3 con el bit 31 de SRC2, que
 * desactiva la corrección de perspectiva de la entrada que leen. Esa
 * entrada llega entonces dividida entre w: con w = 1 no se nota, pero un
 * vértice de la N64 con w ≈ 2000 deja el término casi en cero (RPCS3 lo
 * emula así: tcN * gl_FragCoord.w). Ver docs/rsx-notes.md.
 *
 * Como rsxkit siempre quiere interpolación con perspectiva, el bit se
 * limpia en cada instrucción al subir el programa.
 */
#include <string.h>

#include "internal.h"

/* Cada instrucción son 4 palabras de 32 bits con sus mitades de 16 bits
 * intercambiadas (como las espera el RSX). Palabra 0: bit 0 fin del
 * programa. Palabras 1–3 (SRC0–2): bits 0–1 tipo de registro (2 =
 * constante, cuyo valor viene en los 16 bytes siguientes). */
#define FP_END          0x00000001u
#define FP_SRC_TYPE(w)  ((w) & 3u)
#define FP_SRC_CONST    2u
#define FP_DISABLE_PC   0x80000000u

static inline u32 halves(u32 w)
{
	return (w << 16) | (w >> 16);
}

static void fix_perspective(u32 *ucode, u32 size)
{
	u32 n = size / 4;
	for (u32 i = 0; i + 4 <= n;) {
		u32 *insn = ucode + i;
		u32 w0 = halves(insn[0]);
		int has_const = 0;
		for (int s = 1; s < 4; s++)
			if (FP_SRC_TYPE(halves(insn[s])) == FP_SRC_CONST)
				has_const = 1;
		insn[3] = halves(halves(insn[3]) & ~FP_DISABLE_PC);
		i += has_const ? 8 : 4;
		if (w0 & FP_END)
			break;
	}
}

void *rsxkit_fp_upload(rsxFragmentProgram *fp, u32 *offset)
{
	void *ucode;
	u32 size;
	rsxFragmentProgramGetUCode(fp, &ucode, &size);
	void *buf = rsxMemalign(64, size);
	if (!buf)
		return NULL;
	memcpy(buf, ucode, size);
	fix_perspective(buf, size);
	rsxAddressToOffset(buf, offset);
	return buf;
}
