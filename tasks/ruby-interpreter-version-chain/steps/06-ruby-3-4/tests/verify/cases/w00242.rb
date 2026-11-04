begin
  p(({1 => 2, :b => 3}).invert)
rescue Exception => e
  p e.class
end
