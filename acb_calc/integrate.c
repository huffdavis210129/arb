/*
    Copyright (C) 2017 Fredrik Johansson

    This file is part of Arb.

    Arb is free software: you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License (LGPL) as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.  See <http://www.gnu.org/licenses/>.
*/

#include "acb_calc.h"

static void
quad_simple(acb_t res, acb_calc_func_t f, void * param,
        const acb_t a, const acb_t b, slong prec)
{
    acb_t mid, delta, wide;
    mag_t tmpm;

    acb_init(mid);
    acb_init(delta);
    acb_init(wide);
    mag_init(tmpm);

    /* delta = (b-a)/2 */
    acb_sub(delta, b, a, prec);
    acb_mul_2exp_si(delta, delta, -1);

    /* mid = (a+b)/2 */
    acb_add(mid, a, b, prec);
    acb_mul_2exp_si(mid, mid, -1);

    /* wide = mid +- [delta] */
    acb_set(wide, mid);
    arb_get_mag(tmpm, acb_realref(delta));
    arb_add_error_mag(acb_realref(wide), tmpm);
    arb_get_mag(tmpm, acb_imagref(delta));
    arb_add_error_mag(acb_imagref(wide), tmpm);

    /* Direct evaluation: integral = (b-a) * f([a,b]). */
    f(res, wide, param, 0, prec);
    acb_mul(res, res, delta, prec);
    acb_mul_2exp_si(res, res, 1);

    acb_clear(mid);
    acb_clear(delta);
    acb_clear(wide);
    mag_clear(tmpm);
}

void
acb_calc_integrate(acb_t res, acb_calc_func_t f, void * param,
    const acb_t a, const acb_t b,
    slong goal, const mag_t tol,
    slong deg_limit, slong eval_limit, slong depth_limit,
    int flags,
    slong prec)
{
    acb_ptr as, bs, vs;
    acb_t s, t, u;
    mag_t tmpm, tmpn, new_tol;
    slong depth, depth_max, eval, feval;
    int stopping, real_error;

    acb_init(s);
    acb_init(t);
    acb_init(u);
    mag_init(tmpm);
    mag_init(tmpn);
    mag_init(new_tol);

    if (depth_limit <= 0)
        depth_limit = 2 * prec;
    depth_limit = FLINT_MAX(depth_limit, 1);

    if (eval_limit <= 0)
        eval_limit = 1000 * prec;
    eval_limit = FLINT_MAX(eval_limit, 1);

    goal = FLINT_MAX(goal, 0);
    if (deg_limit <= 0)
        deg_limit = 0.5 * goal + 10;

    /* todo: allocate dynamically */
    as = _acb_vec_init(depth_limit);
    bs = _acb_vec_init(depth_limit);
    vs = _acb_vec_init(depth_limit);

    acb_set(as, a);
    acb_set(bs, b);
    quad_simple(vs, f, param, as, bs, prec);

    depth = depth_max = 1;
    eval = 1;
    stopping = 0;

    /* Adjust absolute tolerance based on new information. */
    acb_get_mag_lower(tmpm, vs);
    mag_mul_2exp_si(tmpm, tmpm, -goal);
    mag_max(new_tol, tol, tmpm);

    acb_zero(s);

    while (depth >= 1)
    {
        if (stopping == 0 && eval >= eval_limit - 1)
        {
            if (flags & ACB_CALC_VERBOSE)
                flint_printf("stopping at eval_limit %wd\n", eval_limit);
            stopping = 1;
            continue;
        }

        acb_set(t, vs + depth - 1);
        mag_hypot(tmpm, arb_radref(acb_realref(t)), arb_radref(acb_imagref(t)));
        acb_sub(u, as + depth - 1, bs + depth - 1, prec);

        /* We are done with this subinterval. */
        if (mag_cmp(tmpm, new_tol) < 0 || acb_contains_zero(u) || stopping)
        {
            acb_add(s, s, t, prec);
            depth--;
            continue;
        }

        /* Attempt using Gauss-Legendre rule. */
        if (acb_is_finite(t))
        {
            /* We know that the result is real. */
            real_error = acb_is_finite(t) && acb_is_real(t);

            feval = acb_calc_integrate_gl_auto_deg(t, f, param, as + depth - 1,
                                bs + depth - 1, new_tol, deg_limit, flags, prec);
            eval += feval;

            /* We are done with this subinterval. */
            if (feval > 0)
            {
                if (real_error)
                    arb_zero(acb_imagref(t));

                acb_add(s, s, t, prec);

                /* Adjust absolute tolerance based on new information. */
                acb_get_mag_lower(tmpm, t);
                mag_mul_2exp_si(tmpm, tmpm, -goal);
                mag_max(new_tol, new_tol, tmpm);

                depth--;
                continue;
            }
        }

        if (depth >= depth_limit - 1)
        {
            if (flags & ACB_CALC_VERBOSE)
                flint_printf("stopping at depth_limit %wd\n", depth_limit);
            stopping = 1;
            continue;
        }

        /* Bisection. */
        /* Interval (num) becomes [mid, b]. */
        acb_set(bs + depth, bs + depth - 1);
        acb_add(as + depth, as + depth - 1, bs + depth - 1, prec);
        acb_mul_2exp_si(as + depth, as + depth, -1);
        /* Interval (num-1) becomes [a, mid]. */
        acb_set(bs + depth - 1, as + depth);

        quad_simple(vs + depth - 1, f, param, as + depth - 1, bs + depth - 1, prec);
        quad_simple(vs + depth, f, param, as + depth, bs + depth, prec);
        eval += 2;

        /* Move the interval with the larger error to the top of the queue. */
        mag_hypot(tmpm, arb_radref(acb_realref(vs + depth - 1)),
            arb_radref(acb_imagref(vs + depth - 1)));
        mag_hypot(tmpn, arb_radref(acb_realref(vs + depth)),
            arb_radref(acb_imagref(vs + depth)));
        if (mag_cmp(tmpm, tmpn) > 0)
        {
            acb_swap(as + depth, as + depth - 1);
            acb_swap(bs + depth, bs + depth - 1);
            acb_swap(vs + depth, vs + depth - 1);
        }

        /* Adjust absolute tolerance based on new information. */
        acb_get_mag_lower(tmpm, vs + depth);
        mag_mul_2exp_si(tmpm, tmpm, -goal);
        mag_max(new_tol, new_tol, tmpm);

        depth++;
        depth_max = FLINT_MAX(depth, depth_max);
    }

    if (flags & ACB_CALC_VERBOSE)
    {
        flint_printf("depth %wd/%wd, eval %wd/%wd\n",
            depth_max, depth_limit, eval, eval_limit);
    }

    acb_set(res, s);

    _acb_vec_clear(as, depth_limit);
    _acb_vec_clear(bs, depth_limit);
    _acb_vec_clear(vs, depth_limit);
    acb_clear(s);
    acb_clear(t);
    acb_clear(u);
    mag_clear(tmpm);
    mag_clear(tmpn);
    mag_clear(new_tol);
}

