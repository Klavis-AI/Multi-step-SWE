begin
  p(%q(a#{1}b\n))
rescue Exception => e
  p e.class
end
