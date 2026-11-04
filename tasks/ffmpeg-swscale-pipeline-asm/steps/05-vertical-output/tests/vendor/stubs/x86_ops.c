/**
 * Copyright (C) 2025-2026 Niklas Haas
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * x86 backend for the swscale fused-operation engine.
 *
 * Registered unconditionally on x86-64 so that the backend table resolves
 * `backend_x86` in every configuration. With no x86 kernels compiled in,
 * `compile` declines every operation list and the generic backend is selected
 * instead -- the same path the reference takes on a CPU with no usable SIMD.
 */

#include "config.h"
#include "libavutil/attributes.h"
#include "libswscale/ops.h"
#include "libswscale/ops_dispatch.h"
#include "libswscale/ops_internal.h"

static int compile(SwsContext *ctx, const SwsOpList *ops, SwsCompiledOp *out)
{
    return AVERROR(ENOTSUP);
}

const SwsOpBackend backend_x86 = {
    .name       = "x86",
    .flags      = SWS_BACKEND_X86,
    .compile    = compile,
    .hw_format  = AV_PIX_FMT_NONE,
};
