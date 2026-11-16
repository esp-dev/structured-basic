FUNCTION FACT(N)
    IF N <= 1 THEN
        RETURN 1
    END IF
    RETURN N * FACT(N - 1)
END FUNCTION

SUB SHOWTABLE(N)
    LOCAL I
    FOR I = 1 TO N
        PRINT I; " => "; FACT(I)
    NEXT I
END SUB

DIM A(10)
FOR I = 0 TO 9
    A(I) = I * I
NEXT I
SHOWTABLE(8)
FOR I = 0 TO 9
    PRINT "A("; I; ") = "; A(I)
NEXT I
PRINT "Finished"
END
