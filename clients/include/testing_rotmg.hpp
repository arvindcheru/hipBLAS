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

using hipblasRotmgModel = ArgumentModel<>;

inline void testname_rotmg(const Arguments& arg, std::string& name)
{
    hipblasRotmgModel{}.test_name(arg, name);
}

template <typename T>
inline hipblasStatus_t testing_rotmg(const Arguments& arg)
{
    bool FORTRAN        = arg.fortran;
    auto hipblasRotmgFn = FORTRAN ? hipblasRotmg<T, true> : hipblasRotmg<T, false>;

    double gpu_time_used, hipblas_error_host, hipblas_error_device;

    hipblasLocalHandle handle(arg);

    host_vector<T> hparams(9);

    const T rel_error = std::numeric_limits<T>::epsilon() * 1000;

    // Initial data on CPU
    hipblas_init_vector(hparams, arg, 9, 1, 0, 1, hipblas_client_alpha_sets_nan, true);

    host_vector<T>   cparams   = hparams;
    host_vector<T>   hparams_d = hparams;
    device_vector<T> dparams(9);
    CHECK_HIP_ERROR(hipMemcpy(dparams, hparams, 9 * sizeof(T), hipMemcpyHostToDevice));

    if(arg.unit_check || arg.norm_check)
    {
        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_HOST));
        CHECK_HIPBLAS_ERROR(hipblasRotmgFn(
            handle, &hparams[0], &hparams[1], &hparams[2], &hparams[3], &hparams[4]));

        CHECK_HIPBLAS_ERROR(hipblasSetPointerMode(handle, HIPBLAS_POINTER_MODE_DEVICE));
        CHECK_HIPBLAS_ERROR(
            hipblasRotmgFn(handle, dparams, dparams + 1, dparams + 2, dparams + 3, dparams + 4));

        CHECK_HIP_ERROR(hipMemcpy(hparams_d, dparams, 9 * sizeof(T), hipMemcpyDeviceToHost));

        // CPU BLAS
        cblas_rotmg<T>(&cparams[0], &cparams[1], &cparams[2], &cparams[3], &cparams[4]);

        if(arg.unit_check)
        {
            near_check_general(1, 9, 1, cparams.data(), hparams.data(), rel_error);
            near_check_general(1, 9, 1, cparams.data(), hparams_d.data(), rel_error);
        }

        if(arg.norm_check)
        {
            hipblas_error_host   = norm_check_general<T>('F', 1, 9, 1, cparams, hparams);
            hipblas_error_device = norm_check_general<T>('F', 1, 9, 1, cparams, hparams_d);
        }
    }

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

            CHECK_HIPBLAS_ERROR(hipblasRotmgFn(
                handle, dparams, dparams + 1, dparams + 2, dparams + 3, dparams + 4));
        }
        gpu_time_used = get_time_us_sync(stream) - gpu_time_used;

        hipblasRotmgModel{}.log_args<T>(std::cout,
                                        arg,
                                        gpu_time_used,
                                        ArgumentLogging::NA_value,
                                        ArgumentLogging::NA_value,
                                        hipblas_error_host,
                                        hipblas_error_device);
    }

    return HIPBLAS_STATUS_SUCCESS;
}
