begin
  p(<<~EOS)
      one
        two
    EOS
rescue Exception => e
  p e.class
end
