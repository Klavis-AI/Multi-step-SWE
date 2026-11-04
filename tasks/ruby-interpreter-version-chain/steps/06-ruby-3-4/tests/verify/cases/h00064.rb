begin
  m = "ab".match(/(?<x>a)(?<y>b)/)
  p m.named_captures
  p m.named_captures(symbolize_names: true)
rescue Exception => e
  p e.class
end
