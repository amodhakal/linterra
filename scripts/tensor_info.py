import onnx

models = [
    "../models/terrain-diffusion/base_model_sim_fp16.onnx",
    "../models/terrain-diffusion/coarse_model_sim_fp16.onnx",
    "../models/terrain-diffusion/decoder_model_sim_fp16.onnx",
]

for model_path in models:
    print(f"Loading {model_path}...")
    model = onnx.load(model_path)

    print(f"\n=== {model_path} ===")
    for inp in model.graph.input:
        dims = [
            d.dim_value if d.dim_value > 0 else "dynamic"
            for d in inp.type.tensor_type.shape.dim
        ]
        print(f"IN {inp.name}: {dims}")
    for out in model.graph.output:
        dims = [
            d.dim_value if d.dim_value > 0 else "dynamic"
            for d in out.type.tensor_type.shape.dim
        ]
        print(f"OUT {out.name}: {dims}")
