begin
  p(({1 => 2, :b => 3}).inspect)
rescue Exception => e
  p e.class
end
