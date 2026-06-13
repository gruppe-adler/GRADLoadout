//------------------------------------------------------------------------------------------------
//! Small templated, stable in-place sorter.
//!
//! Subclass and override Compare() to define ordering. Insertion sort is used deliberately: the
//! arsenal sorts short lists (catalog slices for one slot, a character's slots), where insertion
//! sort is simple, stable, and allocation-free. It is not intended for large datasets.
//!
//! Usage:
//!   class GRAD_NameSorter : GRAD_Sorter<GRAD_ItemRecord>   // plain type, not `ref T`
//!   {
//!       override int Compare(GRAD_ItemRecord a, GRAD_ItemRecord b)
//!       { return a.GetName() < b.GetName() ? -1 : (a.GetName() > b.GetName() ? 1 : 0); }
//!   }
//!   GRAD_NameSorter sorter = new GRAD_NameSorter();
//!   sorter.Sort(items);
class GRAD_Sorter<Class T>
{
	//------------------------------------------------------------------------------------------------
	//! Ordering predicate. Return <0 if a precedes b, >0 if a follows b, 0 if equal.
	//! Must be overridden; the base implementation imposes no ordering.
	int Compare(T a, T b)
	{
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Stable in-place ascending sort according to Compare().
	//! Operates on array<ref T> so it works with the script-managed reference arrays the arsenal
	//! uses (the type parameter T is the plain class type, not `ref T`).
	void Sort(array<ref T> items)
	{
		if (!items)
			return;

		int count = items.Count();
		for (int i = 1; i < count; i++)
		{
			T key = items[i];
			int j = i - 1;

			// Shift larger elements one position right. Using >0 (not >=0) preserves stability.
			while (j >= 0 && Compare(items[j], key) > 0)
			{
				items[j + 1] = items[j];
				j--;
			}

			items[j + 1] = key;
		}
	}
}
