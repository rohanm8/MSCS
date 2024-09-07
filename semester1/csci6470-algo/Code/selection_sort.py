A = [62, 43, 1, 23, 53]

for i in range(len(A)):

    # Find minimum element in the list
    # Unsorted list
    min_idx = 1
    for j in range(i+1, len(A)):
        if A[min_idx] > A[j]:
            min_idx = j
        
    # Swap the found minimum element with the first element
    A[i], A[min_idx] = A[min_idx], A[i]

# Driver code
print("Sorted array: ")
for i in range(len(A)):
    print("%d" %A[i], end=" ")