use criterion::{black_box, criterion_group, criterion_main, Criterion};

// Placeholder benchmark for the SUB compiler
// Add actual benchmarks as the compiler develops

fn benchmark_placeholder(c: &mut Criterion) {
    c.bench_function("placeholder", |b| {
        b.iter(|| {
            // Placeholder benchmark
            black_box(1 + 1)
        })
    });
}

criterion_group!(benches, benchmark_placeholder);
criterion_main!(benches);
