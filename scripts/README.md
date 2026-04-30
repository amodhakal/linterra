# Linterra Scripts

Scripts for the linterra workflow

- **simplifier.sh**: Simplifies the diffusion model
- **quantization.py**: Quantizes down to FP16 from the current FP32

### Model Optimization Results

| Model | Original (FP32) | Simplified (FP32) | Optimized (FP16) | Total Reduction |
| :--- | :--- | :--- | :--- | :--- |
| **Base Model** | 1,936 MB | 968 MB | 484 MB | **~75%** |
| **Coarse Model** | 21.5 MB | 10.7 MB | 5.4 MB | **~75%** |
| **Decoder Model** | 213.5 MB | 106.8 MB | 53.5 MB | **~75%** |
| **TOTAL** | **2,171 MB** | **1,085.5 MB** | **542.9 MB** | **~75%** |
