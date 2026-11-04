begin
  x = <<~"E"
    a
  E
  p x
rescue Exception => e
  p e.class
end
