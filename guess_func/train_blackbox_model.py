import pandas as pd
from sklearn.linear_model import LinearRegression
from sklearn.model_selection import train_test_split
from sklearn.metrics import mean_squared_error
import joblib

# 1. Load your data (replace with your real file path)
# Expected CSV columns: a, b, c, d, out1, out2, out3, out4
data = pd.read_csv("calculation_data.csv")

# 2. Split data into input features (X) and output motor commands (y)
X = data[['a', 'b', 'c', 'd']]
y = data[['out1', 'out2', 'out3', 'out4']]

# 3. Split into training and testing sets
X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)

# 4. Fit a linear regression model
model = LinearRegression()
model.fit(X_train, y_train)

# 5. Evaluate the model
y_pred = model.predict(X_test)
mse = mean_squared_error(y_test, y_pred)
print(f"Mean Squared Error on test set: {mse:.4f}")

# 6. Save the trained model
joblib.dump(model, "calculation_data.pkl")
print("Model saved as calculation_data.pkl")
