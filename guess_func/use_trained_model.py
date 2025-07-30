import joblib
import numpy as np

# Load the model
model = joblib.load("calculation_data.pkl")

# Predict motor values for a new input
# Example: alt=10, roll=0.2, pitch=-0.1, yaw=0.05
new_input = np.array([[10, 0.2, -0.1, 0.05]])
predicted_function = model.predict(new_input)

print("Predicted motor commands:", predicted_function[0])
