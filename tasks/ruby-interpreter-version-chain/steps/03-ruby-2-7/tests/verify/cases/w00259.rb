begin
  p(({[1] => {c: 2}}).each_pair { |k, v| })
rescue Exception => e
  p e.class
end
