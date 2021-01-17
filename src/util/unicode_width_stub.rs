/* Just enough to make getopts work with ascii options */

#![crate_name = "unicode_width"]

pub trait UnicodeWidthStr {
    fn width<'a>(&'a self) -> usize;
}

impl UnicodeWidthStr for str {
    #[inline]
    fn width(&self) -> usize {
        assert!(self.chars().all(|c| c.is_ascii()));
        self.len()
    }
}
