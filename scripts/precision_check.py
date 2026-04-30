import onnx

def check_model_precision(model_path):
    try:
        model = onnx.load(model_path)
        # Check first initializer
        tensor_type = model.graph.initializer[0].data_type
        type_map = {1: "Float32", 10: "Float16", 3: "Int8", 7: "Int64"}
        print(f"{model_path}: {type_map.get(tensor_type, 'Unknown')}")
    except Exception as e:
        print(f"Could not check {model_path}: {e}")

models = [
    "../models/terrain-diffusion/base_model_sim.onnx",
    "../models/terrain-diffusion/coarse_model_sim.onnx",
    "../models/terrain-diffusion/decoder_model_sim.onnx"
]

for m in models:
    check_model_precision(m)