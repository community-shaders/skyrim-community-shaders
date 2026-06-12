# taa-renderdoc-ab.py — runtime A/B check for TAA shader refactors.
#
# Purpose: prove a behavior-preserving claim that bytecode-identity CANNOT (an op-reordering
# restructure, where fxc legitimately emits different-but-equivalent code). It swaps the ISTemporalAA
# pixel shader on a SINGLE captured frame and diffs the output render target. Because the
# inputs (history t1, velocity t2, depth t3, mask t4, alpha t5, cbuffer b2) are frozen from
# that one frame, A (shipping shader) and B (candidate) run on byte-identical inputs — so a
# near-zero output diff means equivalent behavior on a real frame, free of the cross-launch
# and temporal-warmup noise that a two-launch screenshot A/B suffers.
#
# HOW TO RUN: this is executed *inside RenderDoc's embedded Python* via the renderdoc MCP
# `Eval` tool (the global `ctx` HandlerContext must be in scope). It is NOT a standalone
# script. See docs/development/shader-runtime-ab.md for the full operator runbook.
#
# Candidate B (and a baseline A') are supplied as pre-compiled DXBC built with fxc using the
# SAME defines/permutation as the captured build (e.g. PSHADER VR [HDR_OUTPUT]) and with
# /I package/Shaders so includes resolve — identical to tools/verify-shader-refactor.ps1.
# Feeding DXBC (ShaderEncoding.DXBC) avoids RenderDoc's HLSL include/define handling.

import renderdoc as rd
import struct
import os
import math

# eid -> (origPS ResourceId, replacement ResourceId), kept so restore() uses the real objects
# instead of round-tripping through strings. Lives as long as this module's namespace does — if
# your MCP gives a fresh namespace per Eval, do the replace and its restore in the SAME Eval.
_replacements = {}

# Above this baseline-vs-live mean_abs, the floor is implausible (an honest one is a few LSBs):
# the baseline diverged from live, so ab() reports UNVERIFIED-BASELINE instead of judging against
# it. Tune to the observed runtime-vs-fxc residue for your RT format.
FLOOR_SANITY_LIMIT = 1e-2


def _walk(actions):
    for a in actions:
        yield a
        for c in _walk(a.children):
            yield c


def _desc_res(d):
    for attr in ("resource", "resourceId"):
        if hasattr(d, attr):
            return getattr(d, attr)
    return rd.ResourceId.Null


def _as_resid(x):
    # GetShader returns a ResourceId on current RenderDoc; guard older (id, reflection) tuples.
    return x[0] if isinstance(x, tuple) else x


# ISTemporalAA's pixel shader binds exactly these t0..t5 textures — a reliable fingerprint.
# (Output-slot counting is unreliable: D3D11 returns 8 RTV slots and empty ones read as
# "ResourceId::0", which does NOT compare equal to rd.ResourceId.Null.)
_TAA_TEX = {"currentframetex", "historytex", "velocitytex", "depthtex", "masktex", "alphatex"}


def find_candidates(srv_names, reverse=False, max_scan_drawcalls=0, stop_after=0):
    """Find draws whose pixel shader binds ALL of `srv_names` — a resource-fingerprint match that
    is robust across frames/versions (unlike a draw index, which shifts). `srv_names` is any
    iterable of SRV names (case-insensitive). Returns [{eventId, name}]; pass an eventId to ab().
    This is the shader-agnostic finder — `ab()`/`grab_rt()`/`replace_ps_with_dxbc()` are already
    generic, so this plus your shader's fingerprint is all you need to validate a different pass.

    A full forward scan calls SetFrameEvent() once per drawcall, which can time out the replay
    worker on a multi-GB capture. Post-process passes sit near frame end, so pass reverse=True to
    scan from the end and/or max_scan_drawcalls=N to cap how many drawcalls are probed; stop_after=N
    returns as soon as N matches are found. Defaults keep the exhaustive forward scan."""
    want = {s.lower() for s in srv_names}
    def work(ctrl):
        sdfile = ctrl.GetStructuredFile()
        draws = [a for a in _walk(ctrl.GetRootActions()) if (a.flags & rd.ActionFlags.Drawcall)]
        if reverse:
            draws = draws[::-1]
        if max_scan_drawcalls > 0:
            draws = draws[:max_scan_drawcalls]
        res = []
        for a in draws:
            ctrl.SetFrameEvent(a.eventId, True)
            refl = ctrl.GetPipelineState().GetShaderReflection(rd.ShaderStage.Pixel)
            if not refl:
                continue
            names = {r.name.lower() for r in refl.readOnlyResources}
            if want.issubset(names):
                res.append({"eventId": a.eventId, "name": a.GetName(sdfile)})
                if stop_after > 0 and len(res) >= stop_after:
                    break
        return res
    return ctx.replay(work)


def taa_candidates(reverse=False, max_scan_drawcalls=0, stop_after=0):
    """TAA preset: find_candidates() with ISTemporalAA's t0..t5 SRV fingerprint (_TAA_TEX)."""
    return find_candidates(_TAA_TEX, reverse=reverse,
                           max_scan_drawcalls=max_scan_drawcalls, stop_after=stop_after)


def _tex_desc(ctrl, rid):
    for t in ctrl.GetTextures():
        if t.resourceId == rid:
            return t
    return None


def grab_rt(eid, target_index=0):
    """Return (resourceId_str, raw_bytes, (width, height, format_name)) for an output RT."""
    def work(ctrl):
        ctrl.SetFrameEvent(eid, True)
        outs = ctrl.GetPipelineState().GetOutputTargets()
        # Fail soft on an out-of-range or empty (ResourceId::0) slot rather than letting
        # GetTextureData raise and abort the whole Eval session.
        if target_index >= len(outs):
            return (None, b"", (0, 0, "none"))
        rid = _desc_res(outs[target_index])
        if str(rid) == "ResourceId::0":
            return (str(rid), b"", (0, 0, "none"))
        data = bytes(ctrl.GetTextureData(rid, rd.Subresource(0, 0, 0)))
        td = _tex_desc(ctrl, rid)
        meta = (td.width, td.height, str(td.format.Name())) if td else (0, 0, "?")
        return (str(rid), data, meta)
    return ctx.replay(work)


def replace_ps_with_dxbc(eid, dxbc_path, entry="main"):
    """Build a replacement PS from a pre-compiled DXBC file and bind it at the event.
    Returns {ok, errors}. On success the (orig, new) ResourceIds are stored for restore(eid)."""
    # Expand %TEMP%/~ and fail soft on a missing file — a raw open() would abort the whole Eval.
    p = os.path.expandvars(os.path.expanduser(dxbc_path))
    if not os.path.isfile(p):
        return {"ok": False, "errors": "DXBC not found: " + p}
    try:
        with open(p, "rb") as f:
            blob = f.read()
    except OSError as e:
        return {"ok": False, "errors": "cannot read %s: %r" % (p, e)}

    # Undo any prior replacement on this event first, so repeated swaps don't leak target
    # resources or stack replacements (orig would otherwise capture an already-replaced shader).
    if eid in _replacements:
        restore(eid)

    def work(ctrl):
        ctrl.SetFrameEvent(eid, True)
        orig = _as_resid(ctrl.GetPipelineState().GetShader(rd.ShaderStage.Pixel))
        newid, errs = ctrl.BuildTargetShader(entry, rd.ShaderEncoding.DXBC, blob,
                                             rd.ShaderCompileFlags(), rd.ShaderStage.Pixel)
        ok = newid != rd.ResourceId.Null
        if ok:
            ctrl.ReplaceResource(orig, newid)
            ctrl.SetFrameEvent(eid, True)  # re-replay with replacement bound
        return ok, orig, newid, str(errs)

    ok, orig, newid, errs = ctx.replay(work)
    if ok:
        _replacements[eid] = (orig, newid)
    return {"ok": ok, "errors": errs}


def restore(eid):
    """Undo a replacement made at eid, using the stored ResourceIds."""
    pair = _replacements.pop(eid, None)
    if not pair:
        return False
    orig, newid = pair

    def work(ctrl):
        ctrl.RemoveReplacement(orig)
        try:
            ctrl.FreeTargetResource(newid)
        except Exception:
            pass
        ctrl.SetFrameEvent(eid, True)
        return True
    return ctx.replay(work)


def _diff(a, b, meta):
    """Byte-identity fast path + a SAMPLED float-magnitude estimate (no numpy in RenderDoc,
    and full-res TAA RTs are tens of MB — never materialize the whole thing)."""
    # Always populate mean_abs/max_abs so callers (ab) can read them unconditionally.
    out = {"size_a": len(a), "size_b": len(b), "format": meta[2], "dims": [meta[0], meta[1]],
           "mean_abs": None, "max_abs": None}
    if not a or not b:  # fail-soft grab_rt returned empty bytes — not comparable
        out["verdict"] = "NO-DATA"
        return out
    if a == b:  # C-level compare — instant
        out["verdict"] = "IDENTICAL"
        out["bytes_differing"] = 0
        out["mean_abs"] = 0.0
        out["max_abs"] = 0.0
        out["sample_frac_differing"] = 0.0
        return out
    if len(a) != len(b):
        out["verdict"] = "DIFFERS"
        out["note"] = "size mismatch"  # mean_abs stays None -> ab() treats as not-comparable
        return out

    # Per-sample delta units by format: UNORM8 and packed R10G10B10A2 are normalized to [0,1]
    # (the packed format MUST be unpacked by channel, not byte — a 1-LSB 10-bit delta spans byte
    # boundaries and a byte-diff hugely overstates it); float16/float32 are compared in their
    # native units (abs(x-y)). mean_abs is therefore only comparable within one format, which is
    # fine here since baseline and candidate always share the live RT's format. No verdict here:
    # ab() judges relative to the baseline noise floor, because the game's runtime compile differs
    # from offline fxc by a few LSBs even for an identical shader (that residue is the floor).
    fmt = meta[2].lower()
    packed = "10g10b10a2" in fmt
    if packed:
        esz, code = 4, None
    elif "16_float" in fmt:
        esz, code = 2, "<e"
    elif "32_float" in fmt:
        esz, code = 4, "<f"
    elif "8_unorm" in fmt:
        esz, code = 1, None  # 8-bit UNORM per byte
    else:
        # Unknown RT format — refuse to guess. A byte-wise diff on an unhandled packed/sRGB/typeless
        # format would be meaningless; leave mean_abs None so ab() reports NOT-COMPARABLE rather than
        # a misleading EQUIVALENT/DIFFERS.
        out["verdict"] = "NOT-COMPARABLE"
        out["note"] = "unrecognized format: " + meta[2]
        return out

    n = len(a)
    total = n // esz
    stride = max(1, total // 100000)  # cap ~100k samples across the image
    maxabs = absSum = 0.0  # absSum = sum of |Δ| (L1), not squared error — mean_abs = absSum/cnt
    cnt = ndiff = 0
    A2 = ((0, 1023, 1023.0), (10, 1023, 1023.0), (20, 1023, 1023.0), (30, 3, 3.0))
    for i in range(0, total, stride):
        off = i * esz
        if packed:
            px = struct.unpack_from("<I", a, off)[0]
            py = struct.unpack_from("<I", b, off)[0]
            dlt = max(abs(((px >> s) & m) / d - ((py >> s) & m) / d) for (s, m, d) in A2)
        elif code:
            x = struct.unpack_from(code, a, off)[0]
            y = struct.unpack_from(code, b, off)[0]
            # NaN/inf-safe: abs(NaN) AND inf-inf both yield NaN, which silently fails every
            # comparison below and could fake EQUIVALENT. Treat equal values (incl. both-NaN and
            # both-inf) as 0; any NaN/inf-vs-finite mismatch as a max difference.
            dlt = abs(x - y)
            if math.isnan(dlt):
                dlt = 0.0 if (x == y or (math.isnan(x) and math.isnan(y))) else float("inf")
        else:
            dlt = abs(a[off] - b[off]) / 255.0
        if dlt > 0:
            ndiff += 1
        if dlt > maxabs:
            maxabs = dlt
        absSum += dlt
        cnt += 1

    out["sampled_elems"] = cnt
    out["sample_frac_differing"] = (ndiff / cnt) if cnt else 0.0
    out["max_abs"] = maxabs
    out["mean_abs"] = (absSum / cnt) if cnt else 0.0
    out["verdict"] = "COMPARED"  # metrics present; ab() assigns the final baseline-relative verdict
    return out


def ab(eid, candidate_dxbc, baseline_dxbc=None, entry="main"):
    """Full A/B on one event.
    - Captures the live output (A_real).
    - If baseline_dxbc given: swap it in and confirm it matches A_real (validates defines/
      permutation + the DXBC-replace path) before trusting the candidate result.
    - Swaps candidate_dxbc and diffs against A_real. Restores after each swap.
    A failed build is reported as BUILD-FAILED — never a false EQUIVALENT (which would happen
    if we diffed with the original shader still bound)."""
    rid, a_real, meta = grab_rt(eid)
    report = {"eventId": eid, "rt": rid, "rt_meta": {"dims": [meta[0], meta[1]], "format": meta[2]}}

    if baseline_dxbc:
        r = replace_ps_with_dxbc(eid, baseline_dxbc, entry)
        if not r["ok"]:
            report["baseline_vs_live"] = {"verdict": "BUILD-FAILED", "errors": r["errors"]}
        else:
            # finally: guarantee the replacement is undone even if grab/diff raises, so a stale
            # shader can't contaminate later replays.
            try:
                _, a_prime, _ = grab_rt(eid)
                report["baseline_vs_live"] = _diff(a_real, a_prime, meta)
            finally:
                restore(eid)

    r = replace_ps_with_dxbc(eid, candidate_dxbc, entry)
    if not r["ok"]:
        report["candidate_vs_live"] = {"verdict": "BUILD-FAILED", "errors": r["errors"]}
        report["verdict"] = "BUILD-FAILED"
        return report
    try:
        _, b, _ = grab_rt(eid)
        report["candidate_vs_live"] = _diff(a_real, b, meta)
    finally:
        restore(eid)

    # Verdict: judge the candidate's mean_abs against the baseline noise floor (runtime-vs-fxc
    # compile residue). EQUIVALENT if within 3x the floor; DIFFERS otherwise.
    base = report.get("baseline_vs_live") or {}
    floor = base.get("mean_abs")
    cand_mean = report["candidate_vs_live"].get("mean_abs")
    if cand_mean is None:
        # candidate output not comparable (empty RT / size mismatch) — surface, don't crash.
        report["verdict"] = "NOT-COMPARABLE"
    elif floor is not None:
        report["noise_floor_mean"] = floor
        if floor > FLOOR_SANITY_LIMIT:
            # An honest floor is a few LSBs of runtime-vs-fxc residue. A large floor means the
            # baseline diverged from live — wrong defines/permutation, or a baseline compiled from
            # the wrong ref (e.g. dev when the refactor is stacked on an unmerged fix). Judging
            # against it would rubber-stamp almost any candidate EQUIVALENT, so refuse to judge.
            report["verdict"] = "UNVERIFIED-BASELINE"
            report["note"] = ("baseline noise floor %.4g exceeds %.4g — verify defines/permutation "
                              "and that the baseline is the refactor's parent" % (floor, FLOOR_SANITY_LIMIT))
        else:
            report["verdict"] = "EQUIVALENT" if cand_mean <= max(floor * 3.0, 1e-4) else "DIFFERS"
    elif baseline_dxbc:
        # A baseline was requested but did not yield a usable floor (build failed or size
        # mismatch). Do NOT silently fall back to the absolute path — the floor is the whole
        # point of passing a baseline, so the candidate is unjudged.
        report["verdict"] = "UNVERIFIED-BASELINE"
    else:
        # No baseline requested: best-effort absolute tolerance only.
        report["verdict"] = "EQUIVALENT" if cand_mean <= 1e-3 else "DIFFERS"
    return report
