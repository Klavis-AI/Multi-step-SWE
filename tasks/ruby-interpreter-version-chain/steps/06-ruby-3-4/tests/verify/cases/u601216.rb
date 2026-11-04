# frozen_string_literal: false
i = 0
    sum = 0
    begin
      i += 1
      sum += i
    end while i < 10
    p([i, sum])

    i = 0
    sum = 0
    (
      i += 1
      sum += i
    ) while false
    p([i, sum])