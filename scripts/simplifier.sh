uv run onnxsim ../models/terrain-diffusion/base_model.onnx ../models/terrain-diffusion/base_model_sim.onnx --no-large-tensor
uv run onnxsim ../models/terrain-diffusion/coarse_model.onnx ../models/terrain-diffusion/coarse_model_sim.onnx --no-large-tensor
uv run onnxsim ../models/terrain-diffusion/decoder_model.onnx ../models/terrain-diffusion/decoder_model_sim.onnx --no-large-tensor