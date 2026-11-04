begin
  p(:sym.to_proc.lambda?)
rescue Exception => e
  p e.class
end
