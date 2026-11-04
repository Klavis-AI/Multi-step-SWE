begin
  p(({"k" => 1}).min_by { |k, v| k.to_s })
rescue Exception => e
  p e.class
end
