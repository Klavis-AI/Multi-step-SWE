print(("(a(b)c)"):match("%b()"))
print(tostring(("(a(b)c"):match("%b()")))
print(("THE (quick) fox"):gsub("%f[%a]%a+%f[%A]", function(w) return w:upper() end))
