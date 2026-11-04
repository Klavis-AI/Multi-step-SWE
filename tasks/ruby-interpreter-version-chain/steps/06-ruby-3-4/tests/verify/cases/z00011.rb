begin
  p({a: {b: [1, {c: 2}]}})
rescue Exception => e
  p e.class
end
