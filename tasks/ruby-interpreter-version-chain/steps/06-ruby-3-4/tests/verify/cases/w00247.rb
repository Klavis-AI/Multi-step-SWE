begin
  p(({1 => 2, :b => 3}).to_h)
rescue Exception => e
  p e.class
end
