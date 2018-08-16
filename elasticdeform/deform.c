#include <Python.h>
#define NPY_NO_DEPRECATED_API NPY_1_13_API_VERSION
#define NO_IMPORT_ARRAY
#define PY_ARRAY_UNIQUE_SYMBOL Deform_ARRAY_API
#include <numpy/arrayobject.h>
#include "from_scipy.h"
#include "deform.h"


/* map a coordinate outside the borders, according to the requested
     boundary condition: */
static double
map_coordinate(double in, npy_intp len, int mode)
{
    if (in < 0) {
        switch (mode) {
        case NI_EXTEND_MIRROR:
            if (len <= 1) {
                in = 0;
            } else {
                npy_intp sz2 = 2 * len - 2;
                in = sz2 * (npy_intp)(-in / sz2) + in;
                in = in <= 1 - len ? in + sz2 : -in;
            }
            break;
        case NI_EXTEND_REFLECT:
            if (len <= 1) {
                in = 0;
            } else {
                npy_intp sz2 = 2 * len;
                if (in < -sz2)
                    in = sz2 * (npy_intp)(-in / sz2) + in;
                in = in < -len ? in + sz2 : -in - 1;
            }
            break;
        case NI_EXTEND_WRAP:
            if (len <= 1) {
                in = 0;
            } else {
                npy_intp sz = len - 1;
                // Integer division of -in/sz gives (-in mod sz)
                // Note that 'in' is negative
                in += sz * ((npy_intp)(-in / sz) + 1);
            }
            break;
        case NI_EXTEND_NEAREST:
            in = 0;
            break;
        case NI_EXTEND_CONSTANT:
            in = -1;
            break;
        }
    } else if (in > len-1) {
        switch (mode) {
        case NI_EXTEND_MIRROR:
            if (len <= 1) {
                in = 0;
            } else {
                npy_intp sz2 = 2 * len - 2;
                in -= sz2 * (npy_intp)(in / sz2);
                if (in >= len)
                    in = sz2 - in;
            }
            break;
        case NI_EXTEND_REFLECT:
            if (len <= 1) {
                in = 0;
            } else {
                npy_intp sz2 = 2 * len;
                in -= sz2 * (npy_intp)(in / sz2);
                if (in >= len)
                    in = sz2 - in - 1;
            }
            break;
        case NI_EXTEND_WRAP:
            if (len <= 1) {
                in = 0;
            } else {
                npy_intp sz = len - 1;
                in -= sz * (npy_intp)(in / sz);
            }
            break;
        case NI_EXTEND_NEAREST:
            in = len - 1;
            break;
        case NI_EXTEND_CONSTANT:
            in = -1;
            break;
        }
    }

    return in;
}

int
get_spline_interpolation_weights(double x, int order, double *weights)
{
    int i;
    double y, z, t;

    /* Convert x to the delta to the middle knot. */
    x -= floor(order & 1 ? x : x + 0.5);
    y = x;
    z = 1.0 - x;

    switch (order) {
        case 1:
            /* 0 <= x < 1*/
            weights[0] = 1.0 - x;
            break;
        case 2:
            /* -0.5 < x <= 0.5 */
            weights[1] = 0.75 - x * x;
            /* For weights[0] we'd normally do:
             *
             *   y = 1 + x  # 0.5 < y <= 1.5
             *   yy = 1.5 - y  # yy = 0.5 - x
             *   weights[0] = 0.5 * yy * yy
             *
             * So we set y = 0.5 - x directly instead.
             */
            y = 0.5 - x;
            weights[0] = 0.5 * y * y;
            break;
        case 3:
            /* y = x, 0 <= y < 1 */
            weights[1] = (y * y * (y - 2.0) * 3.0 + 4.0) / 6.0;
            /* z = 1 - x, 0 < z <= 1 */
            weights[2] = (z * z * (z - 2.0) * 3.0 + 4.0) / 6.0;
            /*
             * For weights[0] we would normally do:
             *
             *   y += 1.0  # y = 1.0 + x, 1 <= y < 2
             *   yy = 2.0 - y  # yy = 1 - x
             *   weights[0] = yy * yy * yy / 6.0
             *
             * But we already have yy in z.
             */
            weights[0] = z * z * z / 6.0;
            break;
        case 4:
            /* -0.5 < x <= 0.5 */
            t = x * x;
            weights[2] = t * (t * 0.25 - 0.625) + 115.0 / 192.0;
            /* y = 1 + x, 0.5 < y <= 1.5 */
            y = 1.0 + x;
            weights[1] = y * (y * (y * (5.0 - y) / 6.0 - 1.25) + 5.0 / 24.0) +
                         55.0 / 96.0;
            /* z = 1 - x, 0.5 <= z < 1.5 */
            weights[3] = z * (z * (z * (5.0 - z) / 6.0 - 1.25) + 5.0 / 24.0) +
                         55.0 / 96.0;
            /*
             * For weights[0] we would normally do:
             *
             *   y += 1.0  # y = 2.0 + x, 1.5 <= y < 2.5
             *   yy = 2.5 - y  # yy = 0.5 - x
             *  weights[0] = yy**4 / 24.0
             *
             * So we set y = 0.5 - x directly instead.
             */
            y = 0.5 - x;
            t = y * y;
            weights[0] = t * t / 24.0;
            break;
        case 5:
            /* y = x, 0 <= y < 1 */
            t = y * y;
            weights[2] = t * (t * (0.25 - y / 12.0) - 0.5) + 0.55;
            /* z = 1 - x, 0 < z <= 1 */
            t = z * z;
            weights[3] = t * (t * (0.25 - z / 12.0) - 0.5) + 0.55;
            /* y = 1 + x, 1 <= y < 2 */
            y += 1.0;
            weights[1] = y * (y * (y * (y * (y / 24.0 - 0.375) + 1.25) - 1.75)
                              + 0.625) + 0.425;
            /* z = 2 - x, 1 < z <= 2 */
            z += 1.0;
            weights[4] = z * (z * (z * (z * (z / 24.0 - 0.375) + 1.25) - 1.75)
                              + 0.625) + 0.425;
            /* For weights[0] we would normally do:
             *
             *   y += 1.0  # y = 2.0 + x, 2 <= y < 3
             *   yy = 3.0 - y  # yy = 1.0 - x
             *   weights[0] = yy**5 / 120.0
             *
             * So we set y = 2.0 - y = 1.0 - x directly instead.
             */
            y = 1.0 - x;
            t = y * y;
            weights[0] = y * t * t / 120.0;
            break;
        default:
            return 1; /* Unsupported spline order. */
    }

    /* All interpolation weights add to 1.0, so use it for the last one. */
    weights[order] = 1.0;
    for (i = 0; i < order; ++i) {
        weights[order] -= weights[i];
    }

    return 0;
}

/* copy row of coordinate array from location at _p to _coor */
#define CASE_MAP_COORDINATES(_TYPE, _type, _p, _coor, _rank, _stride) \
case _TYPE:                                                           \
{                                                                     \
    npy_intp _hh;                                                     \
    for (_hh = 0; _hh < _rank; ++_hh) {                               \
        _coor[_hh] = *(_type *)_p;                                    \
        _p += _stride;                                                \
    }                                                                 \
}                                                                     \
break

#define CASE_INTERP_COEFF(_TYPE, _type, _coeff, _pi, _idx) \
case _TYPE:                                                \
    _coeff = *(_type *)(_pi + _idx);                       \
    break

#define CASE_INTERP_OUT(_TYPE, _type, _po, _t) \
case _TYPE:                                    \
    *(_type *)_po = (_type)_t;                 \
    break

#define CASE_INTERP_OUT_UINT(_TYPE, _type, _po, _t)  \
case NPY_##_TYPE:                                    \
    _t = _t > 0 ? _t + 0.5 : 0;                      \
    _t = _t > NPY_MAX_##_TYPE ? NPY_MAX_##_TYPE : t; \
    _t = _t < 0 ? 0 : t;                             \
    *(_type *)_po = (_type)_t;                       \
    break

#define CASE_INTERP_OUT_INT(_TYPE, _type, _po, _t)   \
case NPY_##_TYPE:                                    \
    _t = _t > 0 ? _t + 0.5 : _t - 0.5;               \
    _t = _t > NPY_MAX_##_TYPE ? NPY_MAX_##_TYPE : t; \
    _t = _t < NPY_MIN_##_TYPE ? NPY_MIN_##_TYPE : t; \
    *(_type *)_po = (_type)_t;                       \
    break

int DeformGrid(int ninputs,
               PyArrayObject** inputs, PyArrayObject* displacement, PyArrayObject* output_offset,
               PyArrayObject** outputs, int* orders, int* modes, double* cvals)
{
    char **pos = NULL, **pis = NULL, *pd = NULL;
    npy_intp **edge_offsets = NULL, **data_offsets = NULL, *filter_sizes = NULL, dfilter_size;
    npy_intp **dedge_offsets = NULL, **ddata_offsets = NULL;
    npy_intp ftmp[NPY_MAXDIMS], *fcoordinates = NULL, *foffsets = NULL;
    npy_intp dftmp[NPY_MAXDIMS], *dfcoordinates = NULL, *dfoffsets = NULL;
    npy_intp kk, hh, ll, jj, ii;
    npy_intp size, max_filter_size = 0;
    double **splvals = NULL, **dsplvals = NULL;
    npy_intp idimensions[NPY_MAXDIMS];
    npy_intp *istrides = NULL;
    npy_intp odimensions[NPY_MAXDIMS];
    npy_intp ddimensions[NPY_MAXDIMS], dstrides[NPY_MAXDIMS];
    npy_intp ooffsets[NPY_MAXDIMS];
    npy_intp ncontrolpoints[NPY_MAXDIMS];
    double displ[NPY_MAXDIMS];
    NI_Iterator *ios = NULL;
    int irank = 0;

    /* spline order for deplacement */
    int dorder = 3;

    NPY_BEGIN_THREADS_DEF;

    NPY_BEGIN_THREADS;

    for(kk = 0; kk < PyArray_NDIM(inputs[0]); kk++) {
        idimensions[kk] = PyArray_DIM(inputs[0], kk);
        odimensions[kk] = PyArray_DIM(outputs[0], kk);
    }
    irank = PyArray_NDIM(inputs[0]);

    for (kk = 0; kk < PyArray_NDIM(displacement); kk++) {
        ddimensions[kk] = PyArray_DIM(displacement, kk);
        dstrides[kk] = PyArray_STRIDE(displacement, kk);
    }

    istrides = malloc(ninputs * irank * sizeof(npy_intp));
    if (NPY_UNLIKELY(!istrides)) {
        NPY_END_THREADS;
        PyErr_NoMemory();
        goto exit;
    }
    for (ii = 0; ii < ninputs; ii++) {
        for(kk = 0; kk < irank; kk++) {
            istrides[ii * irank + kk] = PyArray_STRIDE(inputs[ii], kk);
        }
    }

    /* check if the output is cropped */
    for(kk = 0; kk < irank; kk++) {
        ooffsets[kk] = 0;
    }
    if (output_offset) {
        for(kk = 0; kk < irank; kk++) {
            ooffsets[kk] = *(npy_intp *)(PyArray_GETPTR1(output_offset, kk));
        }
    }

    /* number of control points in each dimension */
    for (kk = 0; kk < irank; kk++) {
        ncontrolpoints[kk] = PyArray_DIM(displacement, kk + 1);
    }

    /* offsets used at the borders: */
    edge_offsets = malloc(ninputs * irank * sizeof(npy_intp*));
    data_offsets = malloc(ninputs * irank * sizeof(npy_intp*));
    if (NPY_UNLIKELY(!edge_offsets || !data_offsets)) {
        NPY_END_THREADS;
        PyErr_NoMemory();
        goto exit;
    }
    for(jj = 0; jj < ninputs * irank; jj++)
        data_offsets[jj] = NULL;
    for(ii = 0; ii < ninputs; ii++) {
        for(jj = 0; jj < irank; jj++) {
            data_offsets[ii * irank + jj] = malloc((orders[ii] + 1) * sizeof(npy_intp));
            if (NPY_UNLIKELY(!data_offsets[ii * irank + jj])) {
                NPY_END_THREADS;
                PyErr_NoMemory();
                goto exit;
            }
        }
    }

    /* offsets in desplacement used at the borders: */
    dedge_offsets = malloc(irank * sizeof(npy_intp*));
    ddata_offsets = malloc(irank * sizeof(npy_intp*));
    if (NPY_UNLIKELY(!dedge_offsets || !ddata_offsets)) {
        NPY_END_THREADS;
        PyErr_NoMemory();
        goto exit;
    }
    for(jj = 0; jj < irank; jj++)
        ddata_offsets[jj] = NULL;
    for(jj = 0; jj < irank; jj++) {
        ddata_offsets[jj] = malloc((dorder + 1) * sizeof(npy_intp));
        if (NPY_UNLIKELY(!ddata_offsets[jj])) {
            NPY_END_THREADS;
            PyErr_NoMemory();
            goto exit;
        }
    }

    npy_int perimeter = 0;
    for(jj = 0; jj < irank; jj++) {
        perimeter += odimensions[jj];
    }
    /* will hold the deplacement spline coefficients: */
    dsplvals = malloc(irank * perimeter * sizeof(double*));
    if (NPY_UNLIKELY(!dsplvals)) {
        NPY_END_THREADS;
        PyErr_NoMemory();
        goto exit;
    }
    for(jj = 0; jj < irank * perimeter; jj++)
        dsplvals[jj] = NULL;
    for(jj = 0; jj < irank * perimeter; jj++) {
        dsplvals[jj] = malloc((dorder + 1) * sizeof(double));
        if (NPY_UNLIKELY(!dsplvals[jj])) {
            NPY_END_THREADS;
            PyErr_NoMemory();
            goto exit;
        }
    }

    /* will hold the spline coefficients: */
    splvals = malloc(ninputs * irank * sizeof(double*));
    if (NPY_UNLIKELY(!splvals)) {
        NPY_END_THREADS;
        PyErr_NoMemory();
        goto exit;
    }
    for(jj = 0; jj < ninputs * irank; jj++)
        splvals[jj] = NULL;
    for(ii = 0; ii < ninputs; ii++) {
        for(jj = 0; jj < irank; jj++) {
            splvals[ii * irank + jj] = malloc((orders[ii] + 1) * sizeof(double));
            if (NPY_UNLIKELY(!splvals[ii * irank + jj])) {
                NPY_END_THREADS;
                PyErr_NoMemory();
                goto exit;
            }
        }
    }

    /* compute filter size for each input */
    max_filter_size = 0;
    filter_sizes = malloc(ninputs * sizeof(npy_intp));
    if (NPY_UNLIKELY(!filter_sizes)) {
        NPY_END_THREADS;
        PyErr_NoMemory();
        goto exit;
    }
    for(ii = 0; ii < ninputs; ii++) {
        filter_sizes[ii] = 1;
        for(jj = 0; jj < irank; jj++)
            filter_sizes[ii] *= orders[ii] + 1;
        if (filter_sizes[ii] > max_filter_size)
            max_filter_size = filter_sizes[ii];
    }

    dfilter_size = 1;
    for(jj = 0; jj < irank; jj++)
        dfilter_size *= dorder + 1;

    /* initialize output iterator: */
    ios = malloc(ninputs * sizeof(NI_Iterator));
    if (NPY_UNLIKELY(!ios)) {
        NPY_END_THREADS;
        PyErr_NoMemory();
        goto exit;
    }
    for (ii = 0; ii < ninputs; ii++) {
        if (!NI_InitPointIterator(outputs[ii], &ios[ii]))
            goto exit;
    }

    /* get data pointers: */
    pis = malloc(ninputs * sizeof(char*));
    pos = malloc(ninputs * sizeof(char*));
    if (NPY_UNLIKELY(!pis || !pos)) {
        NPY_END_THREADS;
        PyErr_NoMemory();
        goto exit;
    }
    for (ii = 0; ii < ninputs; ii++) {
        pis[ii] = (void *)PyArray_DATA(inputs[ii]);
        pos[ii] = (void *)PyArray_DATA(outputs[ii]);
    }
    pd = (void *)PyArray_DATA(displacement);

    /* make a table of all possible coordinates within the spline filter: */
    fcoordinates = malloc(ninputs * irank * max_filter_size * sizeof(npy_intp));
    /* make a table of all offsets within the spline filter: */
    foffsets = malloc(ninputs * max_filter_size * sizeof(npy_intp));
    if (NPY_UNLIKELY(!fcoordinates || !foffsets)) {
        NPY_END_THREADS;
        PyErr_NoMemory();
        goto exit;
    }
    for(ii = 0; ii < ninputs; ii++) {
        for(jj = 0; jj < irank; jj++)
            ftmp[jj] = 0;
        kk = 0;
        for(hh = 0; hh < filter_sizes[ii]; hh++) {
            for(jj = 0; jj < irank; jj++)
                fcoordinates[jj + hh * irank + ii * irank * max_filter_size] = ftmp[jj];
            foffsets[hh + ii * max_filter_size] = kk;
            for(jj = irank - 1; jj >= 0; jj--) {
                if (ftmp[jj] < orders[ii]) {
                    ftmp[jj]++;
                    kk += istrides[ii * irank + jj];
                    break;
                } else {
                    ftmp[jj] = 0;
                    kk -= istrides[ii * irank + jj] * orders[ii];
                }
            }
        }
    }

    /* make a table of all possible coordinates within the spline filter: */
    dfcoordinates = malloc(irank * dfilter_size * sizeof(npy_intp));
    /* make a table of all offsets within the spline filter: */
    dfoffsets = malloc(dfilter_size * sizeof(npy_intp));
    if (NPY_UNLIKELY(!dfcoordinates || !dfoffsets)) {
        NPY_END_THREADS;
        PyErr_NoMemory();
        goto exit;
    }
    for(jj = 0; jj < irank; jj++)
        dftmp[jj] = 0;
    kk = 0;
    for(hh = 0; hh < dfilter_size; hh++) {
        for(jj = 0; jj < irank; jj++)
            dfcoordinates[jj + hh * irank] = dftmp[jj];
        dfoffsets[hh] = kk;
        for(jj = irank - 1; jj >= 0; jj--) {
            if (dftmp[jj] < dorder) {
                dftmp[jj]++;
                kk += dstrides[jj + 1];
                break;
            } else {
                dftmp[jj] = 0;
                kk -= dstrides[jj + 1] * dorder;
            }
        }
    }

    /* precompute control points and spline weights for displacement */
    kk = 0;
    for(hh = 0; hh < irank; hh++) {
        for(jj = 0; jj < odimensions[hh]; jj++) {
            double cp = (double)(ncontrolpoints[hh] - 1) * (double)(jj + ooffsets[hh]) / (double)(idimensions[hh] - 1);
            get_spline_interpolation_weights(cp, dorder, dsplvals[kk]);
            kk++;
        }
    }

    size = PyArray_SIZE(outputs[0]);
    for(kk = 0; kk < size; kk++) {
        /* compute deplacement on this dimension */
        int dedge = 0;
        npy_intp ddoffset = 0;
        for(jj = 0; jj < irank; jj++) {
            /* assumption: by definition, cp is inside the deplacement array */
            double cp = (double)(ncontrolpoints[jj] - 1) * (double)(ios[0].coordinates[jj] + ooffsets[jj]) / (double)(idimensions[jj] - 1);
            /* find the filter location along this axis: */
            npy_intp start;
            if (dorder & 1) {
                start = (npy_intp)floor(cp) - dorder / 2;
            } else {
                start = (npy_intp)floor(cp + 0.5) - dorder / 2;
            }
            /* get the offset to the start of the filter: */
            ddoffset += dstrides[jj + 1] * start;
            if (start < 0 || start + dorder >= ddimensions[jj + 1]) {
                /* implement border mapping, if outside border: */
                dedge = 1;
                dedge_offsets[jj] = ddata_offsets[jj];
                for(ll = 0; ll <= dorder; ll++) {
                    npy_intp idx = start + ll;
                    npy_intp len = ddimensions[jj + 1];
                    if (len <= 1) {
                        idx = 0;
                    } else {
                        npy_intp s2 = 2 * len - 2;
                        if (idx < 0) {
                            idx = s2 * (int)(-idx / s2) + idx;
                            idx = idx <= 1 - len ? idx + s2 : -idx;
                        } else if (idx >= len) {
                            idx -= s2 * (int)(idx / s2);
                            if (idx >= len)
                                idx = s2 - idx;
                        }
                    }
                    /* calculate and store the offests at this edge: */
                    dedge_offsets[jj][ll] = dstrides[jj + 1] * (idx - start);
                }
            } else {
                /* we are not at the border, use precalculated offsets: */
                dedge_offsets[jj] = NULL;
            }
        }

        /* iterate over axes: */
        for(hh = 0; hh < irank; hh++) {
            /* compute displacement */
            npy_intp *dff = dfcoordinates;
            const int type_num = PyArray_TYPE(displacement);
            displ[hh] = 0.0;
            for(jj = 0; jj < dfilter_size; jj++) {
                double coeff = 0.0;
                npy_intp idx = 0;

                if (NPY_UNLIKELY(dedge)) {
                    for(ll = 0; ll < irank; ll++) {
                        if (dedge_offsets[ll])
                            idx += dedge_offsets[ll][dff[ll]];
                        else
                            idx += dff[ll] * dstrides[ll + 1];
                    }
                } else {
                    idx = dfoffsets[jj];
                }
                idx += dstrides[0] * hh;
                idx += ddoffset;
                switch (type_num) {
                    CASE_INTERP_COEFF(NPY_BOOL, npy_bool,
                                      coeff, pd, idx);
                    CASE_INTERP_COEFF(NPY_UBYTE, npy_ubyte,
                                      coeff, pd, idx);
                    CASE_INTERP_COEFF(NPY_USHORT, npy_ushort,
                                      coeff, pd, idx);
                    CASE_INTERP_COEFF(NPY_UINT, npy_uint,
                                      coeff, pd, idx);
                    CASE_INTERP_COEFF(NPY_ULONG, npy_ulong,
                                      coeff, pd, idx);
                    CASE_INTERP_COEFF(NPY_ULONGLONG, npy_ulonglong,
                                      coeff, pd, idx);
                    CASE_INTERP_COEFF(NPY_BYTE, npy_byte,
                                      coeff, pd, idx);
                    CASE_INTERP_COEFF(NPY_SHORT, npy_short,
                                      coeff, pd, idx);
                    CASE_INTERP_COEFF(NPY_INT, npy_int,
                                      coeff, pd, idx);
                    CASE_INTERP_COEFF(NPY_LONG, npy_long,
                                      coeff, pd, idx);
                    CASE_INTERP_COEFF(NPY_LONGLONG, npy_longlong,
                                      coeff, pd, idx);
                    CASE_INTERP_COEFF(NPY_FLOAT, npy_float,
                                      coeff, pd, idx);
                    CASE_INTERP_COEFF(NPY_DOUBLE, npy_double,
                                      coeff, pd, idx);
                default:
                    NPY_END_THREADS;
                    PyErr_SetString(PyExc_RuntimeError,
                                    "data type not supported");
                    goto exit;
                }
                /* calculate the interpolated value: */
                double **cur_dsplvals = dsplvals;
                for(ll = 0; ll < irank; ll++) {
                    if (dorder > 0)
                        coeff *= cur_dsplvals[ios[0].coordinates[ll]][dff[ll]];
                    cur_dsplvals += odimensions[ll];
                }
                displ[hh] += coeff;
                dff += irank;
            }
        }


        /* iterate over all inputs */
        for(ii = 0; ii < ninputs; ii++) {
            int constant = 0, edge = 0;
            npy_intp offset = 0;

            /* iterate over axes: */
            for(hh = 0; hh < irank; hh++) {
                /* compute the coordinate: coordinate of output voxel io.coordinates[hh] + displacement displ[hh] */
                /* if the input coordinate is outside the borders, map it: */
                double cc = map_coordinate(ios[ii].coordinates[hh] + ooffsets[hh] + displ[hh], idimensions[hh], modes[ii]);
                if (cc > -1.0) {
                    /* find the filter location along this axis: */
                    npy_intp start;
                    if (orders[ii] & 1) {
                        start = (npy_intp)floor(cc) - orders[ii] / 2;
                    } else {
                        start = (npy_intp)floor(cc + 0.5) - orders[ii] / 2;
                    }
                    /* get the offset to the start of the filter: */
                    offset += istrides[ii * irank + hh] * start;
                    if (start < 0 || start + orders[ii] >= idimensions[hh]) {
                        /* implement border mapping, if outside border: */
                        edge = 1;
                        edge_offsets[ii * irank + hh] = data_offsets[ii * irank + hh];
                        for(ll = 0; ll <= orders[ii]; ll++) {
                            npy_intp idx = start + ll;
                            npy_intp len = idimensions[hh];
                            if (len <= 1) {
                                idx = 0;
                            } else {
                                npy_intp s2 = 2 * len - 2;
                                if (idx < 0) {
                                    idx = s2 * (int)(-idx / s2) + idx;
                                    idx = idx <= 1 - len ? idx + s2 : -idx;
                                } else if (idx >= len) {
                                    idx -= s2 * (int)(idx / s2);
                                    if (idx >= len)
                                        idx = s2 - idx;
                                }
                            }
                            /* calculate and store the offests at this edge: */
                            edge_offsets[ii * irank + hh][ll] = istrides[ii * irank + hh] * (idx - start);
                        }
                    } else {
                        /* we are not at the border, use precalculated offsets: */
                        edge_offsets[ii * irank + hh] = NULL;
                    }
                    get_spline_interpolation_weights(cc, orders[ii], splvals[ii * irank + hh]);
                } else {
                    /* we use the constant border condition: */
                    constant = 1;
                    break;
                }
            }

            /* interpolate value for this input */
            double t = 0.0;
            if (!constant) {
                npy_intp *ff = fcoordinates + (ii * irank * max_filter_size);
                const int type_num = PyArray_TYPE(inputs[ii]);
                t = 0.0;
                for(hh = 0; hh < filter_sizes[ii]; hh++) {
                    double coeff = 0.0;
                    npy_intp idx = 0;

                    if (NPY_UNLIKELY(edge)) {
                        for(ll = 0; ll < irank; ll++) {
                            if (edge_offsets[ii * irank + ll])
                                idx += edge_offsets[ii * irank + ll][ff[ll]];
                            else
                                idx += ff[ll] * istrides[ii * irank + ll];
                        }
                    } else {
                        idx = foffsets[hh + ii * max_filter_size];
                    }
                    idx += offset;
                    switch (type_num) {
                        CASE_INTERP_COEFF(NPY_BOOL, npy_bool,
                                          coeff, pis[ii], idx);
                        CASE_INTERP_COEFF(NPY_UBYTE, npy_ubyte,
                                          coeff, pis[ii], idx);
                        CASE_INTERP_COEFF(NPY_USHORT, npy_ushort,
                                          coeff, pis[ii], idx);
                        CASE_INTERP_COEFF(NPY_UINT, npy_uint,
                                          coeff, pis[ii], idx);
                        CASE_INTERP_COEFF(NPY_ULONG, npy_ulong,
                                          coeff, pis[ii], idx);
                        CASE_INTERP_COEFF(NPY_ULONGLONG, npy_ulonglong,
                                          coeff, pis[ii], idx);
                        CASE_INTERP_COEFF(NPY_BYTE, npy_byte,
                                          coeff, pis[ii], idx);
                        CASE_INTERP_COEFF(NPY_SHORT, npy_short,
                                          coeff, pis[ii], idx);
                        CASE_INTERP_COEFF(NPY_INT, npy_int,
                                          coeff, pis[ii], idx);
                        CASE_INTERP_COEFF(NPY_LONG, npy_long,
                                          coeff, pis[ii], idx);
                        CASE_INTERP_COEFF(NPY_LONGLONG, npy_longlong,
                                          coeff, pis[ii], idx);
                        CASE_INTERP_COEFF(NPY_FLOAT, npy_float,
                                          coeff, pis[ii], idx);
                        CASE_INTERP_COEFF(NPY_DOUBLE, npy_double,
                                          coeff, pis[ii], idx);
                    default:
                        NPY_END_THREADS;
                        PyErr_SetString(PyExc_RuntimeError,
                                        "data type not supported");
                        goto exit;
                    }
                    /* calculate the interpolated value: */
                    for(ll = 0; ll < irank; ll++)
                        if (orders[ii] > 0)
                            coeff *= splvals[ii * irank + ll][ff[ll]];
                    t += coeff;
                    ff += irank;
                }
            } else {
                t = cvals[ii];
            }
            /* store output value: */
            switch (PyArray_TYPE(outputs[ii])) {
                CASE_INTERP_OUT(NPY_BOOL, npy_bool, pos[ii], t);
                CASE_INTERP_OUT_UINT(UBYTE, npy_ubyte, pos[ii], t);
                CASE_INTERP_OUT_UINT(USHORT, npy_ushort, pos[ii], t);
                CASE_INTERP_OUT_UINT(UINT, npy_uint, pos[ii], t);
                CASE_INTERP_OUT_UINT(ULONG, npy_ulong, pos[ii], t);
                CASE_INTERP_OUT_UINT(ULONGLONG, npy_ulonglong, pos[ii], t);
                CASE_INTERP_OUT_INT(BYTE, npy_byte, pos[ii], t);
                CASE_INTERP_OUT_INT(SHORT, npy_short, pos[ii], t);
                CASE_INTERP_OUT_INT(INT, npy_int, pos[ii], t);
                CASE_INTERP_OUT_INT(LONG, npy_long, pos[ii], t);
                CASE_INTERP_OUT_INT(LONGLONG, npy_longlong, pos[ii], t);
                CASE_INTERP_OUT(NPY_FLOAT, npy_float, pos[ii], t);
                CASE_INTERP_OUT(NPY_DOUBLE, npy_double, pos[ii], t);
            default:
                NPY_END_THREADS;
                PyErr_SetString(PyExc_RuntimeError, "data type not supported");
                goto exit;
            }
            NI_ITERATOR_NEXT(ios[ii], pos[ii]);
        }
    }

 exit:
    NPY_END_THREADS;
    free(istrides);
    free(edge_offsets);
    free(dedge_offsets);
    if (data_offsets) {
        for(jj = 0; jj < ninputs * irank; jj++)
            free(data_offsets[jj]);
        free(data_offsets);
    }
    if (ddata_offsets) {
        for(jj = 0; jj < irank; jj++)
            free(ddata_offsets[jj]);
        free(ddata_offsets);
    }
    if (dsplvals) {
        for(jj = 0; jj < irank * perimeter; jj++)
            free(dsplvals[jj]);
        free(dsplvals);
    }
    if (splvals) {
        for(jj = 0; jj < ninputs * irank; jj++)
            free(splvals[jj]);
        free(splvals);
    }
    free(filter_sizes);
    free(foffsets);
    free(fcoordinates);
    free(dfoffsets);
    free(dfcoordinates);
    free(pos);
    free(pis);
    free(ios);
    return PyErr_Occurred() ? 0 : 1;
}