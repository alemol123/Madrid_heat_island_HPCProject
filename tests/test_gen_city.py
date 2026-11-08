
import hashlib
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import gen_city  # noqa: E402


def _sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def test_format_and_values():
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "c.city")
        city = gen_city.generate(64, 48, 42)
        gen_city.write_city(p, city)
        assert os.path.getsize(p) == 8 + 64 * 48          # 2 int32 + nx*ny int8
        raw = open(p, "rb").read()
        nx, ny = np.frombuffer(raw[:8], dtype="<i4")
        assert (nx, ny) == (64, 48)                       # header order: nx then ny
        back = gen_city.read_city(p)
        assert back.shape == (48, 64) and np.array_equal(back, city)
        assert city.min() >= 0 and city.max() <= 5
        assert (city == gen_city.GREEN_ROOF).sum() == 0   # baseline has no green roofs
        # row-major: cell (i=3, j=5) is at byte offset 8 + 5*64 + 3
        assert raw[8 + 5 * 64 + 3] == int(city[5, 3])


def test_reproducible_and_seed_dependent():
    a = gen_city.generate(200, 200, 7)
    b = gen_city.generate(200, 200, 7)
    c = gen_city.generate(200, 200, 8)
    assert np.array_equal(a, b)
    assert not np.array_equal(a, c)


def test_has_all_baseline_types():
    city = gen_city.generate(512, 512, 42)
    present = set(np.unique(city).tolist())
    assert {0, 1, 2, 3, 4} <= present


def test_non_square_and_odd_sizes():
    for nx, ny in [(8, 8), (63, 17), (301, 129)]:
        c = gen_city.generate(nx, ny, 1)
        assert c.shape == (ny, nx)


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
