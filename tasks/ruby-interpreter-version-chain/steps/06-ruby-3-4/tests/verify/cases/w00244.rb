begin
  p(({1 => 2, :b => 3}).sort_by { |k, v| k.to_s })
rescue Exception => e
  p e.class
end
