import onnx
from onnxconverter_common import float16

models = [
    "../models/terrain-diffusion/base_model_sim.onnx",
    "../models/terrain-diffusion/coarse_model_sim.onnx",
    "../models/terrain-diffusion/decoder_model_sim.onnx",
]

for model_path in models:
    print(f"Loading {model_path}...")
    model = onnx.load(model_path)

    print(f"Converting {model_path} to FP16...")
    model_fp16 = float16.convert_float_to_float16(model)

    out_path = model_path.replace(".onnx", "_fp16.onnx")
    onnx.save(model_fp16, out_path)
    print(f"Saved: {out_path}")
