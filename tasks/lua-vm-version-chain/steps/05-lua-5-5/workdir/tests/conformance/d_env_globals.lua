x_global = 10
print(x_global, _G.x_global, _G["x_global"])
_G.y_global = 20
print(y_global)
print(_G == _G._G, type(_G))
print(_VERSION)
