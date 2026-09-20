# ── Verified compiler cache (ccache, opt-out CBM_NO_CCACHE=1) ──
# Sourced by env.sh and by build harnesses that do not take the full env
# (rust-scanner-mutation.sh's per-seed snapshot builds).
# Activated through ccache's masquerade directories so $CC keeps its plain
# name everywhere (verify_compiler, make, link lines are untouched).
# Zero-staleness guarantee: CCACHE_COMPILERCHECK=content keys every entry on
# the CONTENT of the compiler binary plus the fully preprocessed translation
# unit, so a cache hit is provably the identical compilation — a stale or
# foreign cache can only MISS, never return wrong output. No CCACHE_BASEDIR
# and no path rewriting: debug-info and sanitizer report paths stay exact.
if [[ "${CBM_NO_CCACHE:-0}" != "1" ]] && command -v ccache >/dev/null 2>&1; then
    for _cbm_ccache_masq in \
        /usr/lib/ccache \
        /opt/homebrew/opt/ccache/libexec \
        /usr/local/opt/ccache/libexec \
        /clang64/lib/ccache/bin \
        /clangarm64/lib/ccache/bin; do
        if [[ -d "$_cbm_ccache_masq" ]]; then
            case ":$PATH:" in
            *":$_cbm_ccache_masq:"*) ;;
            *) PATH="$_cbm_ccache_masq:$PATH" ;;
            esac
        fi
    done
    unset _cbm_ccache_masq
    export PATH
    export CCACHE_COMPILERCHECK=content
fi
