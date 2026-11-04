begin
  m = "ab".match(/(?<f>a)(?<s>b)/)
  p m.deconstruct
  p m.deconstruct_keys([:f])
rescue Exception => e
  p e.class
end
