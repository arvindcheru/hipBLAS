/* ************************************************************************
 * Copyright (C) 2016-2022 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * ************************************************************************ */

#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "testing_common.hpp"

/* ============================================================================================ */

using hipblasAxpyBatchedModel = ArgumentModel<e_N, e_alpha, e_incx, e_incy, e_batch_count>;

inline void testname_axpy_batched(const Arguments& arg, std::string& name)
{
    hipblasAxpyBatchedModel{}.test_name(arg, name);
}

template <typename T>
inline hipblasStatus_t testing_axpy_batched(const Arguments& arg)
{
    bool FORTRAN = arg.fortran;
    auto hipblasAxpyBatchedFn
        = FORTRAN ? hipblasAxpyBatched<T, true> : hipblasAxpyBatched<T, false>;

    int N           = arg.N;
    int incx        = arg.incx;
    int incy        = arg.incy;
    int batch_count = arg.batch_count;
    int abs_incy    = incy < 0 ? -incy : incy;

    hipblasLocalHandle handle(arg);

    // argument sanity check, quick return if input parameters are invalid before allocating invalid
    // memory
    if(N <= 0 || batch_count <= 0)
    {
        CHECK_HIPBLAS_ERROR(
            hipblasAxpyBatchedFn(handle, N, nullptr, nullptr, incx, nullptr, incy, batch_count));
        return HIPBLAS_STATUS_SUCCESS;
    }

    T alpha = arg.get_alpha<T>();

    double gpu_time_used, hipblas_error_host, hipblas_error_device;

    // Naming: dX is in GPU (device) memory. hK is in CPU (host) memory, plz follow this practice
    host_batch_vector<T> hx(N, incx, batch_count);
    host_batch_vector<T> hy_host(N, incy, batch_count);
    host_batch_vector<T> hy_device(N, incy, batch_count);
    host_batch_vector<T> hx_cpu(N, incx, batch_count);
    host_batch_vector<T> hy_cpu(N, incy, batch_count);

    device_batch_vector<T> dx(N, incx, batch_count);
    device_batch_vector<T> dy_host(N, incy, batch_count);
    device_batch_vector<T> dy_device(N, incy, batch_count);
    device_vector<T>       d_alpha(1);
    CHECK_HIP_ERROR(dx.memcheck());
    CHECK_HIP_ERROR(dy_host.memcheck());
    CHECK_HIP_ERROR(dy_device.memcheck());

    hipblas_init_vector(hx, arg, hipblas_client_alpha_sets_nan, true);
    hipblas_init_vector(hy_host, arg, hipblas_client_alpha_sets_nan, false);
    hy_device.copy_from(hy_host);
    hx_cpu.copy_from(hx);
    hy_cpu.copy_from(hy_host);

    CHECK_HIP_ERROR(dx.transfer_from(hx));
    CHECK_HIP_ERROR(dy_host.transfer_from(hy_host));
    CHECK_HIP_ERROR(dy_device.transfer_from(hy_device));
    CHECK_HIP_ERROR(hipMemcpy(d_alpha, &alpha, sizeof(T), hipMemcpyHostToDevice));

    if(arg.unit_check || arg.norm_check)
    {
        /* =====================================================================
                    HIPBLAS
        =================================================================== */
        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));
        CHECK_HIPBLAS_ERROR(hipblasAxpyBatchedFn(handle,
                                                 N,
                                                 d_alpha,
                                                 dx.ptr_on_device(),
                                                 incx,
                                                 dy_device.ptr_on_device(),
                                                 incy,
                                                 batch_count));

        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_HOST));
        CHECK_HIPBLAS_ERROR(hipblasAxpyBatchedFn(handle,
                                                 N,
                                                 &alpha,
                                                 dx.ptr_on_device(),
                                                 incx,
                                                 dy_host.ptr_on_device(),
                                                 incy,
                                                 batch_count));

        CHECK_HIP_ERROR(hy_host.transfer_from(dy_host));
        CHECK_HIP_ERROR(hy_device.transfer_from(dy_device));

        /* =====================================================================
                    CPU BLAS
        =================================================================== */
        for(int b = 0; b < batch_count; b++)
        {
            cblas_axpy<T>(N, alpha, hx_cpu[b], incx, hy_cpu[b], incy);
        }

        // enable unit check, notice unit check is not invasive, but norm check is,
        // unit check and norm check can not be interchanged their order
        if(arg.unit_check)
        {
            unit_check_general<T>(1, N, batch_count, abs_incy, hy_cpu, hy_host);
            unit_check_general<T>(1, N, batch_count, abs_incy, hy_cpu, hy_device);
        }
        if(arg.norm_check)
        {
            hipblas_error_host
                = norm_check_general<T>('F', 1, N, abs_incy, hy_cpu, hy_host, batch_count);
            hipblas_error_device
                = norm_check_general<T>('F', 1, N, abs_incy, hy_cpu, hy_device, batch_count);
        }

    } // end of if unit check

    if(arg.timing)
    {
        hipStream_t stream;
        CHECK_HIPBLAS_ERROR(hipblasGetStream(handle, &stream));
        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));

        int runs = arg.cold_iters + arg.iters;
        for(int iter = 0; iter < runs; iter++)
        {
            if(iter == arg.cold_iters)
                gpu_time_used = get_time_us_sync(stream);

            CHECK_HIPBLAS_ERROR(hipblasAxpyBatchedFn(handle,
                                                     N,
                                                     d_alpha,
                                                     dx.ptr_on_device(),
                                                     incx,
                                                     dy_device.ptr_on_device(),
                                                     incy,
                                                     batch_count));
        }
        gpu_time_used = get_time_us_sync(stream) - gpu_time_used;

        hipblasAxpyBatchedModel{}.log_args<T>(std::cout,
                                              arg,
                                              gpu_time_used,
                                              axpy_gflop_count<T>(N),
                                              axpy_gbyte_count<T>(N),
                                              hipblas_error_host,
                                              hipblas_error_device);
    }

    return HIPBLAS_STATUS_SUCCESS;
}
